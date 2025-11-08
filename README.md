# ESP32-C3 Data Logger

A low-power data logger using ESP32-C3 Super Mini with DS1308 RTC, with sampling synchronized to Coordinated Universal Time (UTC). Suitable for sampling periods of tens of seconds or longer.

## Features

- Configurable sampling period
- Sampling aligned to midnight UTC
- Deep sleep between samples for power efficiency
- WiFi connectivity
- DS1308 (DS1307-compatible) RTC maintains time across deep sleep cycles
- NTP time sync on scheduled boots (based on configured RTC ppm drift and configured allowed drift in seconds)

## Hardware Requirements

- ESP32-C3 Super Mini module
- DS1308 RTC module connected via I²C to ESP32-C3
- Pull-up resistors for I²C:
  - 10kΩ on GPIO8 (SDA) - often already present on some modules
  - 10kΩ on GPIO9 (SCL) - required if not present externally

## Pin Configuration

| Component | Interface | Pin |
|-----------|-----------|-----|
| DS1308 RTC | I²C SDA | GPIO8 |
| DS1308 RTC | I²C SCL | GPIO9 |

## Getting Started in Arduino IDE

Follow the [Getting Started with the ESP32-C3 Super Mini](https://randomnerdtutorials.com/getting-started-esp32-c3-super-mini/) tutorial.

Open `esp32c3_data_logger/esp32c3_data_logger.ino` in Arduino IDE.

## Required Libraries

Install with dependencies via Arduino Library Manager:

- `RTClib` by Adafruit (v2.1.1+)

Built-in ESP32 libraries:

- `WiFi.h`
- `time.h`
- `esp_sntp.h`
- `Wire.h`

## Project Structure

```
esp32c3_data_logger/
├── esp32c3_data_logger.ino     # Main sketch
└── Secrets.h                   # WiFi and timezone configuration
README.md                       # This file
```

## Configuration

Create `esp32c3_data_logger/Secrets.h` with your settings:

```cpp
#ifndef SECRETS_H
#define SECRETS_H

// WiFi credentials
const char *wifi_ssid = "YOUR_WIFI_SSID";
const char *wifi_password = "YOUR_WIFI_PASSWORD";

// Time zone (affects local time display only, not UTC)
// See https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv
const char *time_zone = "EET-2EEST,M3.5.0/3,M10.5.0/4";  // Example for Finland

#endif // SECRETS_H
```

### Adjusting Sampling Interval

Edit the sampling period in the main sketch by changing `samplingPeriodSeconds`:

```cpp
// in esp32c3_data_logger.ino
constexpr uint64_t samplingPeriodSeconds = 30; // sampling period in seconds
```

Notes:
- The sketch aligns samples to evenly spaced slots from midnight UTC, so the device wakes at the next slot boundary.
- `adjustSleepSeconds` is applied to compensate the measured lag between ESP32-C3 wakeup and setup() start; you can fine-tune it if your sensor read timing differs.
- NTP sync scheduling is computed from expected RTC drift (ppm) and an allowed drift in seconds — see the constants `rtcDriftPpm` and `allowedDriftSeconds` in the sketch.

## How It Works

1. Boot sequence:
   - On every boot the device reads the sensor (via the example `temperatureRead()` call in the sketch) and initializes peripherals/serial.
   - On the first boot (bootCount == 0) it will also scan WiFi networks for debugging.
   - The device increments `bootCount` which persists across deep sleep cycles by residing in ESP32-C3 internal RTC memory.

2. Time synchronization:
   - On scheduled boots (when `bootCount % ntpSyncIntervalSamplingPeriods == 0`) the ESP32 syncs time from NTP servers and then updates the DS1308 RTC from the ESP32 time.
   - On other boots the ESP32 synchronizes its time from the DS1308 RTC.
   - This schedule is calculated from the declared maximum RTC drift (`rtcDriftPpm`) and the allowed drift in seconds (`allowedDriftSeconds`) so NTP syncs occur only as frequently as needed.
   - Jumps in UTC such as leap seconds are not tolerated. There have been no leap seconds since 2015 and they are likely to be phased out from UTC, see [Resolution 4 of the 27th General Conference on Weights and Measures (CGPM), 2022](https://www.bipm.org/en/cgpm-2022/resolution-4). More subtle UTC adjustments, if those are ever introduced, might be tolerated by configuring a large enough maximum ppm drift.

3. Logging and sleep:
   - If `bootCount != 0` the sketch prints a CSV line with the nominal wake timestamp and the sensor value (e.g. `time,temperature_esp32`).
   - The sketch computes the next sampling slot aligned to midnight UTC, applies `adjustSleepSeconds` compensation, and then enters deep sleep until that exact microsecond-aligned wake time.

## Serial Monitor Output

The device prints:
- Boot count and wake timestamps
- RTC and ESP32 time comparisons
- WiFi scan results (first boot only)
- Connection status
- Sleep duration and expected wake time

Example output:

```
============== ESP32-C3 Data Logger ==============
Boot count: 0
Initializing DS1308 RTC ... DONE, got time: 2025-11-08T14:32:16Z
Scanning WiFi ... DONE
0: Namulis-Papetti  (-48 dBm)  SECURED  Matches the configured SSID
1: ############  (-74 dBm)  SECURED
2: #############  (-86 dBm)  SECURED
3: ##########  (-91 dBm)  SECURED
WiFi connecting to Namulis-Papetti ....... DONE, got local ip 192.168.178.58
Syncing time from NTP ......................................... DONE
Syncing DS1308 RTC from ESP32 ... DONE
Current time:
DS1308 RTC 2025-11-08T14:32:41Z
ESP32      2025-11-08T14:32:41.002675Z
Will sleep until 2025-11-08T14:33:00.000000Z
============== ESP32-C3 Data Logger ==============
Boot count: 1
Initializing DS1308 RTC ... DONE, got time: 2025-11-08T14:33:03Z
-----------------data logging-----------------
time,temperature_esp32
2025-11-08T14:33:00.000000Z,26.900000
----------------------------------------------
Compensated sample lag: 0.000056 seconds
WiFi connecting to Namulis-Papetti ....... DONE, got local ip 192.168.178.58
Boots remaining until NTP sync: 18
Syncing ESP32 time from DS1308 RTC ... DONE
Setup start time (estimated): 2025-11-08T14:32:59.952583Z
Sample time shift from nominal (estimated): -0.047 seconds (mean: -0.047, RMS: 0.047)
Current time:
DS1308 RTC 2025-11-08T14:33:06Z
ESP32      2025-11-08T14:33:06.002224Z
Will sleep until 2025-11-08T14:33:30.000000Z
============== ESP32-C3 Data Logger ==============
Boot count: 2
Initializing DS1308 RTC ... DONE, got time: 2025-11-08T14:33:32Z
-----------------data logging-----------------
time,temperature_esp32
2025-11-08T14:33:30.000000Z,25.900000
----------------------------------------------
Compensated sample lag: 0.000054 seconds
WiFi connecting to Namulis-Papetti ....... DONE, got local ip 192.168.178.58
Boots remaining until NTP sync: 17
Syncing ESP32 time from DS1308 RTC ... DONE
Setup start time (estimated): 2025-11-08T14:33:29.921571Z
Sample time shift from nominal (estimated): -0.078 seconds (mean: -0.063, RMS: 0.065)
Current time:
DS1308 RTC 2025-11-08T14:33:35Z
ESP32      2025-11-08T14:33:35.002267Z
Will sleep until 2025-11-08T14:34:00.000000Z
```

## Troubleshooting

* **RTC not found**: Check I²C connections and pull-up resistors (especially GPIO9).
* **Wi-Fi won't connect**: 
  - Verify SSID/password in Secrets.h
  - Ensure 2.4GHz network (ESP32-C3 doesn't support 5GHz)
  - Try commenting out: `WiFi.setTxPower(WIFI_POWER_8_5dBm);`
* **Time sync fails**: Check internet connectivity and NTP server accessibility
* **RTC time drift**: DS1308 accuracy depends on crystal quality and temperature. Adjust `rtcDriftPpm` or `allowedDriftSeconds` in the sketch to change NTP sync frequency.

## Authors

Olli Niemitalo (Olli.Niemitalo@hamk.fi)

## Copyright

Copyright 2025 HAMK Häme University of Applied Sciences

## License

MIT License - See LICENSE file for details
