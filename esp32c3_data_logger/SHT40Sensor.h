#ifndef SHT40_SENSOR_H
#define SHT40_SENSOR_H

#include "ISensor.h"
#include <Adafruit_SHT4x.h>
#include "Config.h"

// ============================================================================
// SHT40 TEMPERATURE AND HUMIDITY SENSOR
// ============================================================================
// Implements ISensor interface for Sensirion SHT40 I2C sensor.
// Provides high-accuracy temperature and relative humidity measurements.

class SHT40Sensor : public ISensor {
private:
    Adafruit_SHT4x sht4;
    bool sensorFound;
    char errorMsg[64];
    
public:
    /**
     * Constructor - I2C address is auto-detected by library
     */
    SHT40Sensor() : sensorFound(false) {
        errorMsg[0] = '\0';
    }
    
    /**
     * Initialize SHT40 sensor
     * - Attempts to detect sensor on I2C bus
     * - Configures precision and heater settings
     */
    bool begin() override {
        DEBUG_PRINTLN("[SHT40] Initializing...");
        
        // Attempt to initialize sensor
        if (!sht4.begin()) {
            snprintf(errorMsg, sizeof(errorMsg), "SHT40 not found on I2C bus");
            DEBUG_PRINTLN("[SHT40] ERROR: Not found on I2C bus!");
            return false;
        }
        
        // Configure precision (HIGH, MED, or LOW)
        // HIGH = 8.2ms measurement time, ±0.2°C, ±2% RH
        sht4.setPrecision(SHT4X_HIGH_PRECISION);
        
        // Disable heater (not needed for most applications)
        sht4.setHeater(SHT4X_NO_HEATER);
        
        // Test read to verify functionality
        sensors_event_t humidity, temp;
        if (!sht4.getEvent(&humidity, &temp)) {
            snprintf(errorMsg, sizeof(errorMsg), "Initial read test failed");
            DEBUG_PRINTLN("[SHT40] ERROR: Test read failed!");
            return false;
        }
        
        // Sanity check readings
        if (isnan(temp.temperature) || isnan(humidity.relative_humidity)) {
            snprintf(errorMsg, sizeof(errorMsg), "Invalid initial readings");
            DEBUG_PRINTLN("[SHT40] ERROR: Invalid initial readings!");
            return false;
        }
        
        sensorFound = true;
        DEBUG_PRINTF("[SHT40] Initialized! Temp: %.2f°C, RH: %.2f%%\n", 
                     temp.temperature, humidity.relative_humidity);
        
        return true;
    }
    
    /**
     * Read temperature and humidity from SHT40
     * @param data RawReading structure to populate
     * @return true if successful, false on error
     */
    bool read(RawReading& data) override {
        if (!sensorFound) {
            snprintf(errorMsg, sizeof(errorMsg), "Sensor not initialized");
            return false;
        }
        
        // Read sensor
        sensors_event_t humidity, temp;
        if (!sht4.getEvent(&humidity, &temp)) {
            snprintf(errorMsg, sizeof(errorMsg), "Read failed");
            DEBUG_PRINTLN("[SHT40] ERROR: Read failed!");
            return false;
        }
        
        // Validate temperature reading
        if (isnan(temp.temperature) || temp.temperature < -40.0 || temp.temperature > 125.0) {
            snprintf(errorMsg, sizeof(errorMsg), "Invalid temp: %.2f°C", temp.temperature);
            DEBUG_PRINTF("[SHT40] ERROR: Invalid temperature: %.2f°C\n", temp.temperature);
            return false;
        }
        
        // Validate humidity reading
        if (isnan(humidity.relative_humidity) || 
            humidity.relative_humidity < 0.0 || 
            humidity.relative_humidity > 100.0) {
            snprintf(errorMsg, sizeof(errorMsg), "Invalid RH: %.2f%%", humidity.relative_humidity);
            DEBUG_PRINTF("[SHT40] ERROR: Invalid humidity: %.2f%%\n", humidity.relative_humidity);
            return false;
        }
        
        // Populate data structure
        data.sht40_temp = temp.temperature;
        data.sht40_humidity = humidity.relative_humidity;
        
        return true;
    }
    
    /**
     * Get sensor name for logging
     */
    const char* getName() const override {
        return "SHT40";
    }
    
    /**
     * Check if sensor is responding
     */
    bool isAvailable() override {
        if (!sensorFound) return false;
        
        sensors_event_t humidity, temp;
        return sht4.getEvent(&humidity, &temp);
    }
    
    /**
     * Get measurement time
     * HIGH precision: ~8.2ms
     * MED precision: ~4.5ms
     * LOW precision: ~1.7ms
     */
    uint32_t getMeasurementTimeMs() const override {
        return 10; // High precision + margin
    }
    
    /**
     * Get last error message
     */
    const char* getLastError() const override {
        return errorMsg;
    }
    
    /**
     * Get serial number (if available)
     */
    uint32_t getSerialNumber() {
        if (!sensorFound) return 0;
        // Note: Adafruit_SHT4x doesn't expose serial number in current version
        // This is a placeholder for future implementation
        return 0;
    }
    
    /**
     * Enable/disable built-in heater
     * Useful for removing condensation in humid environments
     */
    void setHeaterEnabled(bool enabled) {
        if (sensorFound) {
            sht4.setHeater(enabled ? SHT4X_HIGH_HEATER_1S : SHT4X_NO_HEATER);
        }
    }
};

#endif // SHT40_SENSOR_H