#ifndef CONFIG_H
#define CONFIG_H

// Secrets
#include "Secrets.h"

// ============================================================================
// PIN DEFINITIONS (ESP32-C3 Specific)
// ============================================================================

// I2C Pins (DS1308 RTC and SHT40)
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// 1-Wire Pin (DS18B20)
#define ONEWIRE_PIN 10

// Analog Pin (SEN0193 Soil Moisture)
#define SOIL_MOISTURE_PIN 0  // ADC1_CH0

// SD Card SPI Pins
#define SD_MOSI_PIN 7
#define SD_MISO_PIN 2
#define SD_SCK_PIN  6
#define SD_CS_PIN   3

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================
// WIFI_SSID and WIFI_PASSWORD are defined in Secrets.h
#define WIFI_TIMEOUT_MS 20000  // 20 seconds

// ============================================================================
// NTP TIME SYNCHRONIZATION
// ============================================================================
#define NTP_SERVER "pool.ntp.org"
#define NTP_TIMEZONE_OFFSET 0  // UTC offset in seconds (0 for UTC)
#define NTP_DAYLIGHT_OFFSET 0  // Daylight saving offset in seconds

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================
// MQTT_USERNAME, MQTT_PASSWORD, MQTT_BROKER, and MQTT_PORT are in Secrets.h
#define MQTT_ENABLED true

// ============================================================================
// TASK TIMING CONFIGURATION (milliseconds)
// ============================================================================
#define SENSOR_READ_INTERVAL_MS    1000    // 1 second
#define AGGREGATION_INTERVAL_MS    60000   // 60 seconds (1 minute)
#define CLOUD_UPLOAD_INTERVAL_MS   300000  // 300 seconds (5 minutes)
#define TIME_SYNC_INTERVAL_MS      86400000 // 24 hours

// ============================================================================
// TASK PRIORITIES (Higher number = higher priority)
// ============================================================================
#define SENSOR_TASK_PRIORITY      3  // Highest - time-critical
#define AGGREGATION_TASK_PRIORITY 2  // Medium
#define LOGGING_TASK_PRIORITY     2  // Medium
#define CLOUD_TASK_PRIORITY       1  // Low
#define TIME_SYNC_TASK_PRIORITY   0  // Lowest

// ============================================================================
// TASK STACK SIZES (bytes)
// ============================================================================
#define SENSOR_TASK_STACK      4096
#define AGGREGATION_TASK_STACK 4096
#define LOGGING_TASK_STACK     8192  // Larger for SD operations
#define CLOUD_TASK_STACK       8192  // Larger for network operations
#define TIME_SYNC_TASK_STACK   4096

// ============================================================================
// QUEUE SIZES
// ============================================================================
#define RAW_READING_QUEUE_SIZE 60    // Store up to 60 raw readings
#define AGGREGATED_DATA_QUEUE_SIZE 10 // Store up to 10 aggregated data points

// ============================================================================
// SD CARD CONFIGURATION
// ============================================================================
#define SD_FILENAME_PREFIX "/data_"  // Files will be named data_YYYYMMDD.csv
#define SD_WRITE_BUFFER_SIZE 512     // Bytes

// ============================================================================
// SENSOR CONFIGURATION
// ============================================================================

// DS18B20 Configuration
#define DS18B20_RESOLUTION 12  // 9, 10, 11, or 12 bits

// SEN0193 Soil Moisture Configuration
#define SOIL_MOISTURE_SAMPLES 10  // Number of samples to average
#define SOIL_MOISTURE_MIN 0       // Dry calibration value
#define SOIL_MOISTURE_MAX 4095    // Wet calibration value

// ADC Configuration (ESP32-C3)
#define ADC_ATTENUATION ADC_11db  // 0-3.3V range
#define ADC_RESOLUTION 12         // 12-bit ADC (0-4095)

// ============================================================================
// DEBUGGING
// ============================================================================
#define DEBUG_ENABLED true
#define SERIAL_BAUD_RATE 115200

// Debug macros
#if DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(fmt, ...)
#endif

// ============================================================================
// ERROR CODES
// ============================================================================
#define ERROR_NONE            0
#define ERROR_RTC_INIT        -1
#define ERROR_SD_INIT         -2
#define ERROR_SENSOR_INIT     -3
#define ERROR_WIFI_CONNECT    -4
#define ERROR_MQTT_CONNECT    -5
#define ERROR_FILE_OPEN       -6
#define ERROR_QUEUE_FULL      -7

#endif // CONFIG_H