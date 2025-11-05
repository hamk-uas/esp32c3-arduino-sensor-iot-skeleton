#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H

#include "ISensor.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Config.h"

// ============================================================================
// DS18B20 DIGITAL TEMPERATURE SENSOR
// ============================================================================
// Implements ISensor interface for DS18B20 1-Wire temperature sensor.
// Supports multiple sensors on same bus, but this implementation uses
// the first detected sensor.

class DS18B20Sensor : public ISensor {
private:
    OneWire oneWire;
    DallasTemperature sensors;
    DeviceAddress sensorAddress;
    bool sensorFound;
    char errorMsg[64];
    
public:
    /**
     * Constructor
     * @param pin GPIO pin number for 1-Wire bus
     */
    DS18B20Sensor(uint8_t pin) 
        : oneWire(pin)
        , sensors(&oneWire)
        , sensorFound(false)
    {
        errorMsg[0] = '\0';
    }
    
    /**
     * Initialize DS18B20 sensor
     * - Scans 1-Wire bus for devices
     * - Configures first found sensor
     * - Sets resolution to configured value
     */
    bool begin() override {
        DEBUG_PRINTLN("[DS18B20] Initializing...");
        
        sensors.begin();
        
        // Find first sensor on bus
        if (!sensors.getAddress(sensorAddress, 0)) {
            snprintf(errorMsg, sizeof(errorMsg), "No DS18B20 found on bus");
            DEBUG_PRINTLN("[DS18B20] ERROR: Not found!");
            return false;
        }
        
        // Configure resolution (9, 10, 11, or 12 bits)
        sensors.setResolution(sensorAddress, DS18B20_RESOLUTION);
        
        // Verify sensor responds
        sensors.requestTemperatures();
        float temp = sensors.getTempC(sensorAddress);
        
        if (temp == DEVICE_DISCONNECTED_C || temp < -55.0 || temp > 125.0) {
            snprintf(errorMsg, sizeof(errorMsg), "Sensor read test failed");
            DEBUG_PRINTLN("[DS18B20] ERROR: Initial read failed!");
            return false;
        }
        
        sensorFound = true;
        DEBUG_PRINTF("[DS18B20] Found! Address: 0x%02X%02X%02X%02X%02X%02X%02X%02X\n",
                     sensorAddress[0], sensorAddress[1], sensorAddress[2], sensorAddress[3],
                     sensorAddress[4], sensorAddress[5], sensorAddress[6], sensorAddress[7]);
        DEBUG_PRINTF("[DS18B20] Resolution: %d-bit, Initial temp: %.2f°C\n", 
                     DS18B20_RESOLUTION, temp);
        
        return true;
    }
    
    /**
     * Read temperature from DS18B20
     * @param data RawReading structure to populate
     * @return true if successful, false on error
     */
    bool read(RawReading& data) override {
        if (!sensorFound) {
            snprintf(errorMsg, sizeof(errorMsg), "Sensor not initialized");
            return false;
        }
        
        // Request temperature conversion
        sensors.requestTemperatures();
        
        // Read temperature
        float tempC = sensors.getTempC(sensorAddress);
        
        // Validate reading
        if (tempC == DEVICE_DISCONNECTED_C) {
            snprintf(errorMsg, sizeof(errorMsg), "Device disconnected");
            DEBUG_PRINTLN("[DS18B20] ERROR: Device disconnected!");
            return false;
        }
        
        // Sanity check (DS18B20 range is -55°C to +125°C)
        if (tempC < -55.0 || tempC > 125.0) {
            snprintf(errorMsg, sizeof(errorMsg), "Out of range: %.2f°C", tempC);
            DEBUG_PRINTF("[DS18B20] ERROR: Temperature out of range: %.2f°C\n", tempC);
            return false;
        }
        
        // Populate data structure
        data.ds18b20_temp = tempC;
        
        return true;
    }
    
    /**
     * Get sensor name for logging
     */
    const char* getName() const override {
        return "DS18B20";
    }
    
    /**
     * Check if sensor is responding
     */
    bool isAvailable() override {
        if (!sensorFound) return false;
        
        sensors.requestTemperatures();
        float temp = sensors.getTempC(sensorAddress);
        return (temp != DEVICE_DISCONNECTED_C);
    }
    
    /**
     * Get measurement time based on resolution
     * Resolution  Time
     * 9-bit       93.75 ms
     * 10-bit      187.5 ms
     * 11-bit      375 ms
     * 12-bit      750 ms
     */
    uint32_t getMeasurementTimeMs() const override {
        switch (DS18B20_RESOLUTION) {
            case 9:  return 94;
            case 10: return 188;
            case 11: return 375;
            case 12: return 750;
            default: return 750;
        }
    }
    
    /**
     * Get last error message
     */
    const char* getLastError() const override {
        return errorMsg;
    }
    
    /**
     * Get device address as string
     */
    String getAddressString() {
        if (!sensorFound) return "Unknown";
        
        String addr = "";
        for (uint8_t i = 0; i < 8; i++) {
            if (sensorAddress[i] < 16) addr += "0";
            addr += String(sensorAddress[i], HEX);
        }
        addr.toUpperCase();
        return addr;
    }
};

#endif // DS18B20_SENSOR_H