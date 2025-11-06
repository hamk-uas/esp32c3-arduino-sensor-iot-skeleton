# ESP32-C3 Data Logger

## Hardware Requirements

- ESP32-C3 Super Mini module
- DS1308 RTC module connected with I²C to ESP32-C3 pins gpio8=SDA and gpio9=SCL
- Pull-up resistors for I²C:
  - 10kΩ on gpio8 (already available in ESP32-C3 Super Mini module) 
  - 10kΩ on gpio9 for I²C

## Pin Configuration

| Component | Interface | Pin |
|-----------|-----------|-----|
| DS1308 RTC | I²C SDA | GPIO8 |
| DS1308 RTC | I²C SCL | GPIO9 |

## Getting started in Arduino IDE

Follow the [Getting Started with the ESP32-C3 Super Mini](https://randomnerdtutorials.com/getting-started-esp32-c3-super-mini/) tutorial.

Open `esp32c3_data_logger/esp32c3_data_logger.ino` in Arduino IDE.

## Required Libraries

Install with dependencies via Arduino Library Manager:

- `RTClib` by Adafruit (v2.1.1+)

Built-in ESP32 libraries:

- `WiFi.h`
- `time.h`

## Project Structure

```
esp32c3_data_logger/
├── esp32c3_data_logger.ino     # Main sketch
└── Secrets.h                   # Secrets
README.md                       # This file
```

## Configuration

Edit `esp32c3_data_logger/Secrets.h` to customize:

```cpp
#ifndef SECRETS_H
#define SECRETS_H

// WiFi
const char *ssid = "MY_WIFI_SSID";
const char *password = "MY_WIFI_PASSWORD";

#endif // SECRETS_H
```

Edit `esp32c3_data_logger/esp32c3_data_logger.ino` to customize

```cpp
// NTP server
#define NTP_SERVER "pool.ntp.org"
```

## Getting Started

1. **Install libraries** via Arduino Library Manager
2. **Edit Secrets.h** with your Wi-Fi and MQTT credentials
5. **Upload sketch** to ESP32-C3
6. **Monitor Serial** output at 115200 baud

## Troubleshooting

**RTC not found**: Check I²C connections and pull-up resistors
**Wi-Fi won't connect**: Check SSID/password, ensure 2.4GHz network

## License

MIT License - See LICENSE file for details
