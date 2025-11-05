#ifndef RAW_READING_H
#define RAW_READING_H

#include <Arduino.h>
#include <time.h>

// ============================================================================
// RAW SENSOR READING STRUCTURE
// ============================================================================
// This structure is used to pass individual sensor readings from the
// Sensor Task to the Aggregation Task via FreeRTOS queue.
// All temperature values in Celsius, humidity in %RH, moisture as raw ADC.

struct RawReading {
    time_t timestamp;           // Unix epoch time (UTC) from DS1308 RTC
    float ds18b20_temp;         // DS18B20 temperature in °C
    float sht40_temp;           // SHT40 temperature in °C
    float sht40_humidity;       // SHT40 relative humidity in %RH
    float sen0193_moisture_raw; // SEN0193 raw ADC value (0-4095)
    
    // Constructor to initialize with invalid values
    RawReading() 
        : timestamp(0)
        , ds18b20_temp(NAN)
        , sht40_temp(NAN)
        , sht40_humidity(NAN)
        , sen0193_moisture_raw(NAN)
    {}
};

// ============================================================================
// AGGREGATED DATA STRUCTURE
// ============================================================================
// This structure contains statistical aggregates calculated over a time
// window (e.g., 1 minute). Passed from Aggregation Task to Logging/Cloud Tasks.

struct AggregatedData {
    time_t start_timestamp;     // Start of aggregation window (UTC)
    time_t end_timestamp;       // End of aggregation window (UTC)
    uint16_t sample_count;      // Number of raw samples aggregated
    
    // DS18B20 Statistics
    float ds18b20_avg;
    float ds18b20_min;
    float ds18b20_max;
    
    // SHT40 Temperature Statistics
    float sht40_temp_avg;
    float sht40_temp_min;
    float sht40_temp_max;
    
    // SHT40 Humidity Statistics
    float sht40_hum_avg;
    float sht40_hum_min;
    float sht40_hum_max;
    
    // SEN0193 Soil Moisture Statistics
    float soil_moisture_avg;
    float soil_moisture_min;
    float soil_moisture_max;
    
    // Constructor to initialize with invalid values
    AggregatedData() 
        : start_timestamp(0)
        , end_timestamp(0)
        , sample_count(0)
        , ds18b20_avg(NAN)
        , ds18b20_min(NAN)
        , ds18b20_max(NAN)
        , sht40_temp_avg(NAN)
        , sht40_temp_min(NAN)
        , sht40_temp_max(NAN)
        , sht40_hum_avg(NAN)
        , sht40_hum_min(NAN)
        , sht40_hum_max(NAN)
        , soil_moisture_avg(NAN)
        , soil_moisture_min(NAN)
        , soil_moisture_max(NAN)
    {}
};

// ============================================================================
// SYSTEM STATUS STRUCTURE
// ============================================================================
// Used for monitoring system health and resource usage

struct SystemStatus {
    uint32_t uptime_seconds;
    uint32_t free_heap;
    uint32_t min_free_heap;
    bool wifi_connected;
    bool mqtt_connected;
    bool sd_card_ok;
    uint32_t total_readings;
    uint32_t failed_readings;
    uint32_t sd_write_errors;
    uint32_t mqtt_errors;
    
    SystemStatus()
        : uptime_seconds(0)
        , free_heap(0)
        , min_free_heap(0)
        , wifi_connected(false)
        , mqtt_connected(false)
        , sd_card_ok(false)
        , total_readings(0)
        , failed_readings(0)
        , sd_write_errors(0)
        , mqtt_errors(0)
    {}
};

#endif // RAW_READING_H