# ESP32-C3 Data Logger

A low-power data logger using ESP32-C3 Super Mini with DS1308 RTC for precise time-based sampling.

Coordinated Universal Time (UTC) is used as referene time. Jumps such as leap seconds are not tolerated. There have been no leap seconds since 2015 and they are likely to be phased out from UTC anyway, see [Resolution 4 of the 27th General Conference on Weights and Measures (CGPM), 2022](https://www.bipm.org/en/cgpm-2022/resolution-4). More subtle UTC adjustments, if those are ever introduced, might be tolerated by configuring a large maximum ppm drift.

## Features

- Configurable sampling period
- Sampling aligned to midnight UTC
- Deep sleep between samples for power efficiency
- WiFi connectivity
- NTP time sync on first boot
- DS1308 RTC maintains time across deep sleep cycles

## Hardware Requirements

- ESP32-C3 Super Mini module
- DS1308 RTC module connected via I²C to ESP32-C3
- Pull-up resistors for I²C:
  - 10kΩ on GPIO8 (SDA) - already available in ESP32-C3 Super Mini module
  - 10kΩ on GPIO9 (SCL) - required external resistor

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
const char *time_zone = "EET-2EEST,M3.5.0/3,M10.5.0/4";  // Finland

#endif // SECRETS_H
```

### Adjusting Sampling Interval

Edit `samplingPeriodMicros` in the main sketch:

```cpp
constexpr uint64_t samplingPeriodMicros = MICROS_PER_SECOND * 30; // 30 seconds
```

## How It Works

1. **First Boot**: 
   - Scans for WiFi networks
   - Connects to configured WiFi
   - Syncs time from NTP server
   - Syncs DS1308 RTC from ESP32 time
   
2. **Subsequent Boots**:
   - Syncs ESP32 time from DS1308 RTC
   - Prints nominal wake timestamp
   - Performs data logging operations

3. **Sleep Cycle**:
   - Calculates next sampling time aligned to midnight UTC
   - Enters deep sleep until next sample
   - RTC maintains time during deep sleep

## Getting Started

1. **Install libraries** via Arduino Library Manager
2. **Create Secrets.h** with your WiFi credentials and timezone
3. **Adjust sampling interval** if needed (default: 30 seconds)
4. **Upload sketch** to ESP32-C3
5. **Monitor Serial** output at 115200 baud

## Serial Monitor Output

The device prints:
- Boot count and wake timestamps
- RTC and ESP32 time comparisons
- WiFi scan results (first boot only)
- Connection status
- Sleep duration and expected wake time

## Troubleshooting

* **RTC not found**: Check I²C connections and pull-up resistors (especially GPIO9)
* **Wi-Fi won't connect**: 
  - Verify SSID/password in Secrets.h
  - Ensure 2.4GHz network (ESP32-C3 doesn't support 5GHz)
  - Try commenting out: `WiFi.setTxPower(WIFI_POWER_8_5dBm);`
* **Time sync fails**: Check internet connectivity and NTP server accessibility
* **RTC time drift**: DS1308 accuracy depends on crystal quality and temperature

## License

MIT License - See LICENSE file for details
