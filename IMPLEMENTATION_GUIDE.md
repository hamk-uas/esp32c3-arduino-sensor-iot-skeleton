# ESP32-C3 Data Logger - Implementation Guide

## Quick Start

### 1. Hardware Setup

#### Required Components
- ESP32-C3 development board (e.g., ESP32-C3-DevKitM-1)
- DS1308 RTC module with CR2032 battery
- DS18B20 waterproof temperature sensor
- SHT40 temperature/humidity sensor
- SEN0193 capacitive soil moisture sensor
- Micro SD card module
- SD card (8GB+, FAT32 formatted)
- Breadboard and jumper wires

#### Wiring Diagram

```
ESP32-C3          Component
========          =========

GPIO8 (SDA) -----> DS1308 SDA, SHT40 SDA
GPIO9 (SCL) -----> DS1308 SCL, SHT40 SCL
                   (Add 10kΩ pull-ups to 3.3V if not on modules)

GPIO10      -----> DS18B20 Data (with 4.7kΩ pull-up to 3.3V)

GPIO0 (ADC) -----> SEN0193 Signal

GPIO7 (MOSI)-----> SD Card MOSI
GPIO2 (MISO)-----> SD Card MISO
GPIO6 (SCK) -----> SD Card SCK
GPIO3 (CS)  -----> SD Card CS

3.3V        -----> All VCC pins
GND         -----> All GND pins
```

### 2. Software Setup

#### Install Arduino IDE
1. Download Arduino IDE 2.x from arduino.cc
2. Install ESP32 board support:
   - Go to File → Preferences
   - Add to Additional Boards Manager URLs:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to Tools → Board → Boards Manager
   - Search for "esp32" and install "esp32 by Espressif"
   - Select Board: "ESP32C3 Dev Module"

#### Install Required Libraries
Open Arduino IDE → Tools → Manage Libraries and install:

1. **RTClib** by Adafruit (v2.1.1+)
2. **DallasTemperature** by Miles Burton (v3.9.0+)
3. **OneWire** by Paul Stoffregen (v2.3.7+)
4. **Adafruit SHT4x Library** (v1.0.2+)
5. **Adafruit BusIO** (dependency, auto-installed)
6. **PubSubClient** by Nick O'Leary (v2.8+)

### 3. Project Setup

#### Download the Project
```bash
git clone https://github.com/yourusername/esp32c3-data-logger.git
cd esp32c3-data-logger
```

#### Configure Settings
1. Open `Config.h`
2. Update WiFi credentials:
   ```cpp
   #define WIFI_SSID "your-network-name"
   #define WIFI_PASSWORD "your-password"
   ```
3. Update MQTT settings (if using):
   ```cpp
   #define MQTT_BROKER "mqtt.example.com"
   #define MQTT_PORT 1883
   #define MQTT_TOPIC "sensors/esp32c3/data"
   ```

#### Upload to ESP32-C3
1. Connect ESP32-C3 via USB
2. Select correct COM port: Tools → Port
3. Set upload speed: Tools → Upload Speed → 921600
4. Click Upload button
5. Open Serial Monitor (115200 baud)

## System Architecture

### Task Overview

The system uses **5 FreeRTOS tasks** running concurrently:

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-C3 Task Architecture                │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  [Time Sync Task] ←──────── NTP Server                      │
│   Priority: 0                                                │
│   Runs: Startup + Daily                                      │
│        │                                                      │
│        ↓ (Updates RTC)                                       │
│                                                               │
│  [Sensor Task] ←──────── DS1308 RTC (timestamp)             │
│   Priority: 3            DS18B20, SHT40, SEN0193            │
│   Runs: Every 1s                                             │
│        │                                                      │
│        ↓ (RawReading Queue)                                  │
│                                                               │
│  [Aggregation Task]                                          │
│   Priority: 2                                                │
│   Runs: Every 60s                                            │
│        │                                                      │
│        ↓ (AggregatedData Queue)                              │
│        │                                                      │
│        ├─────→ [SD Logging Task] ──→ SD Card                │
│        │       Priority: 2                                   │
│        │       Runs: On-demand                               │
│        │                                                      │
│        └─────→ [Cloud Task] ──────→ MQTT Broker             │
│                Priority: 1                                   │
│                Runs: Every 5min                              │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

```
1. Sensor Task (every 1s):
   - Read timestamp from RTC
   - Read all sensors
   - Package as RawReading
   - Send to queue
   
2. Aggregation Task (every 60s):
   - Collect all RawReadings from queue
   - Calculate min/max/avg for each sensor
   - Package as AggregatedData
   - Send to output queues
   
3. SD Logging Task (on-demand):
   - Receive AggregatedData from queue
   - Append to daily CSV file
   - Thread-safe via mutex
   
4. Cloud Task (every 5min):
   - Receive AggregatedData from queue
   - Connect to WiFi + MQTT
   - Upload as JSON
   - Disconnect to save power
```

## File Structure

```
esp32c3_data_logger/
│
├── esp32c3_data_logger.ino    # Main sketch with setup() and loop()
│
├── Config.h                    # All configuration constants
│   - Pin definitions
│   - WiFi/MQTT credentials
│   - Task priorities and timing
│   - Debug settings
│
├── RawReading.h                # Data structure definitions
│   - RawReading (sensor data)
│   - AggregatedData (statistics)
│   - SystemStatus (health monitoring)
│
├── ISensor.h                   # Abstract sensor interface
│   - Pure virtual functions
│   - Defines contract for all sensors
│
├── Sensor Implementations:
│   ├── DS1308Sensor.h          # RTC time provider
│   ├── DS18B20Sensor.h         # 1-Wire temperature
│   ├── SHT40Sensor.h           # I²C temp/humidity
│   └── SEN0193Sensor.h         # Analog soil moisture
│
├── Task Implementations:
│   ├── SensorTask.cpp          # High-priority sensor reading
│   ├── AggregationTask.cpp     # Data processing & statistics
│   ├── LoggingTask.cpp         # SD card file I/O
│   ├── CloudTask.cpp           # WiFi + MQTT upload
│   └── TimeSync.cpp            # NTP synchronization
│
└── README.md                   # User documentation
```

## Adding a New Sensor

### Step 1: Create Sensor Class

Create `NewSensor.h`:

```cpp
#ifndef NEW_SENSOR_H
#define NEW_SENSOR_H

#include "ISensor.h"
#include "Config.h"

class NewSensor : public ISensor {
private:
    // Add sensor-specific variables
    uint8_t pin;
    char errorMsg[64];
    
public:
    NewSensor(uint8_t p) : pin(p) {
        errorMsg[0] = '\0';
    }
    
    bool begin() override {
        // Initialize sensor hardware
        // Return true if successful
        DEBUG_PRINTLN("[NewSensor] Initializing...");
        // ... initialization code ...
        return true;
    }
    
    bool read(RawReading& data) override {
        // Read sensor value
        // Populate appropriate field in data structure
        // Return true if successful
        float value = /* read sensor */;
        data.new_sensor_value = value;
        return true;
    }
    
    const char* getName() const override {
        return "NewSensor";
    }
    
    const char* getLastError() const override {
        return errorMsg;
    }
};

#endif
```

### Step 2: Add Field to RawReading

Edit `RawReading.h`:

```cpp
struct RawReading {
    time_t timestamp;
    float ds18b20_temp;
    float sht40_temp;
    float sht40_humidity;
    float sen0193_moisture_raw;
    float new_sensor_value;  // ADD THIS
    
    RawReading() 
        : timestamp(0)
        , ds18b20_temp(NAN)
        , sht40_temp(NAN)
        , sht40_humidity(NAN)
        , sen0193_moisture_raw(NAN)
        , new_sensor_value(NAN)  // ADD THIS
    {}
};
```

### Step 3: Instantiate in SensorTask

Edit `SensorTask.cpp`:

```cpp
#include "NewSensor.h"  // ADD THIS

bool initializeSensors() {
    // ... existing sensors ...
    
    // NEW SENSOR
    NewSensor* newSensor = new NewSensor(NEW_SENSOR_PIN);
    if (newSensor->begin()) {
        activeSensors.push_back(newSensor);
        DEBUG_PRINTLN("[SENSOR_TASK] NewSensor added");
    } else {
        DEBUG_PRINTLN("[SENSOR_TASK] NewSensor init failed");
        delete newSensor;
    }
    
    return true;
}
```

### Step 4: Update Aggregation (if needed)

If you want statistics for the new sensor, edit `AggregationTask.cpp`.

### Step 5: Update CSV Headers

Edit `LoggingTask.cpp` to add column in CSV header.

## Serial Commands

Connect to ESP32-C3 at 115200 baud and use these commands:

| Command | Description |
|---------|-------------|
| `s` | Print comprehensive system status |
| `t` | Print current RTC time |
| `r` | Print current sensor readings |
| `f` | List all files on SD card |
| `h` | Print heap memory information |
| `n` | Manually trigger NTP time sync |

## Troubleshooting

### SD Card Issues

**Problem**: `SD card mount failed!`
- Check wiring (especially CS pin)
- Ensure card is FAT32 formatted
- Try different SD card (some cards incompatible)
- Check 3.3V power supply can handle current

**Problem**: `Failed to create file`
- SD card might be write-protected
- Card full or corrupted
- Reformat card as FAT32

### Sensor Issues

**Problem**: `DS18B20 not found!`
- Check 1-Wire wiring
- Ensure 4.7kΩ pull-up resistor present
- Try different sensor (could be damaged)

**Problem**: `SHT40 not found on I2C bus!`
- Check I²C wiring (SDA/SCL)
- Verify 3.3V power supply
- Try I2C scanner sketch to detect address

**Problem**: `RTC not running`
- RTC needs time set via NTP
- Check CR2032 battery in RTC module
- Verify I²C communication

### WiFi/Network Issues

**Problem**: `WiFi connection timeout!`
- Check SSID and password in Config.h
- Ensure 2.4GHz WiFi (ESP32-C3 doesn't support 5GHz)
- Move closer to router
- Check WiFi signal strength

**Problem**: `MQTT connection failed`
- Verify MQTT broker address and port
- Check username/password if required
- Test broker with MQTT client (e.g., MQTT.fx)
- Ensure broker allows external connections

### Time Sync Issues

**Problem**: `Failed to get time from NTP!`
- Check WiFi connection
- Try different NTP server (e.g., "time.google.com")
- Check firewall isn't blocking NTP (UDP port 123)

### Memory Issues

**Problem**: `Task watchdog triggered`
- Increase task stack size in Config.h
- Check for infinite loops in sensor read
- Monitor heap usage with 'h' command

**Problem**: `Out of memory`
- Reduce queue sizes in Config.h
- Check for memory leaks
- Monitor min free heap

## Performance Tuning

### Adjusting Task Priorities

Higher number = higher priority. Default:
- Sensor Reading: 3 (time-critical)
- Aggregation: 2
- SD Logging: 2
- Cloud Upload: 1
- Time Sync: 0

Modify in `Config.h`:
```cpp
#define SENSOR_TASK_PRIORITY 3
```

### Adjusting Timing Intervals

```cpp
#define SENSOR_READ_INTERVAL_MS    1000    // Read sensors every 1s
#define AGGREGATION_INTERVAL_MS    60000   // Aggregate every 60s
#define CLOUD_UPLOAD_INTERVAL_MS   300000  // Upload every 5min
```

### Power Optimization

To reduce power consumption:
1. Increase aggregation interval
2. Increase cloud upload interval
3. Disable WiFi when not needed (already done)
4. Use deep sleep between readings (requires code modification)

## Data Format

### CSV File Format

Files are created daily: `/data_YYYYMMDD.csv`

```csv
timestamp_start,timestamp_end,samples,ds18b20_avg,ds18b20_min,ds18b20_max,sht40_temp_avg,sht40_temp_min,sht40_temp_max,sht40_hum_avg,sht40_hum_min,sht40_hum_max,soil_avg,soil_min,soil_max
1730822400,1730822460,60,22.50,22.38,22.63,23.15,23.02,23.28,45.20,44.80,45.60,1850,1820,1880
```

### MQTT JSON Format

```json
{
  "device": "esp32c3-logger",
  "start": 1730822400,
  "end": 1730822460,
  "samples": 60,
  "ds18b20": {"avg": 22.50, "min": 22.38, "max": 22.63},
  "sht40_temp": {"avg": 23.15, "min": 23.02, "max": 23.28},
  "sht40_humidity": {"avg": 45.20, "min": 44.80, "max": 45.60},
  "soil_moisture": {"avg": 1850, "min": 1820, "max": 1880}
}
```

## Best Practices

### 1. Always Use UTC Time
- All timestamps are in UTC
- Convert to local time in visualization layer
- Avoids DST ambiguity

### 2. Handle Sensor Failures Gracefully
- System continues even if some sensors fail
- Invalid readings stored as NaN
- Check sensor status with 'r' command

### 3. Monitor System Health
- Use 's' command to check status
- Watch for increasing error counts
- Monitor free heap memory

### 4. SD Card Maintenance
- Format card regularly (monthly)
- Check for corruption
- Keep 20% free space
- Use quality SD cards

### 5. Network Reliability
- Cloud task handles connection failures
- Data queued if upload fails
- Automatic reconnection

## License

MIT License - See LICENSE file for details