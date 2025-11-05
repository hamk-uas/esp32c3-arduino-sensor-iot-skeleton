#ifndef SEN0193_SENSOR_H
#define SEN0193_SENSOR_H

#include "ISensor.h"
#include "Config.h"

// ============================================================================
// SEN0193 CAPACITIVE SOIL MOISTURE SENSOR
// ============================================================================
// Implements ISensor interface for analog soil moisture sensor.
// Uses ADC with averaging to reduce noise from ESP32-C3 ADC.

class SEN0193Sensor : public ISensor {
private:
    uint8_t adcPin;
    char errorMsg[64];
    bool initialized;
    
    // Calibration values (should be determined experimentally)
    uint16_t dryValue;  // ADC reading in dry soil/air
    uint16_t wetValue;  // ADC reading in saturated soil
    
public:
    /**
     * Constructor
     * @param pin Analog input pin (ADC)
     * @param dry ADC value for dry calibration (default: 0)
     * @param wet ADC value for wet calibration (default: 4095)
     */
    SEN0193Sensor(uint8_t pin, uint16_t dry = SOIL_MOISTURE_MIN, uint16_t wet = SOIL_MOISTURE_MAX) 
        : adcPin(pin)
        , dryValue(dry)
        , wetValue(wet)
        , initialized(false)
    {
        errorMsg[0] = '\0';
    }
    
    /**
     * Initialize sensor
     * - Configures ADC pin and attenuation
     * - Performs test reading
     */
    bool begin() override {
        DEBUG_PRINTLN("[SEN0193] Initializing...");
        
        // Configure ADC
        analogReadResolution(ADC_RESOLUTION);
        analogSetAttenuation(ADC_ATTENUATION);
        
        // Perform test read
        uint16_t testValue = readRawADC();
        
        if (testValue == 0 || testValue == 4095) {
            snprintf(errorMsg, sizeof(errorMsg), 
                     "ADC stuck at %d - check wiring", testValue);
            DEBUG_PRINTF("[SEN0193] WARNING: ADC reading %d (may indicate issue)\n", testValue);
            // Don't fail - sensor might be at extreme value legitimately
        }
        
        initialized = true;
        DEBUG_PRINTF("[SEN0193] Initialized! Pin: %d, Initial ADC: %d\n", 
                     adcPin, testValue);
        DEBUG_PRINTF("[SEN0193] Calibration - Dry: %d, Wet: %d\n", 
                     dryValue, wetValue);
        
        return true;
    }
    
    /**
     * Read soil moisture value
     * @param data RawReading structure to populate
     * @return true if successful, false on error
     */
    bool read(RawReading& data) override {
        if (!initialized) {
            snprintf(errorMsg, sizeof(errorMsg), "Sensor not initialized");
            return false;
        }
        
        // Read averaged ADC value
        uint16_t rawValue = readRawADC();
        
        // Store raw ADC value (0-4095)
        data.sen0193_moisture_raw = static_cast<float>(rawValue);
        
        return true;
    }
    
    /**
     * Get sensor name for logging
     */
    const char* getName() const override {
        return "SEN0193";
    }
    
    /**
     * Check if sensor is available
     * For analog sensors, always returns true once initialized
     */
    bool isAvailable() override {
        return initialized;
    }
    
    /**
     * Get measurement time (includes averaging)
     */
    uint32_t getMeasurementTimeMs() const override {
        return SOIL_MOISTURE_SAMPLES * 2; // ~2ms per sample
    }
    
    /**
     * Get last error message
     */
    const char* getLastError() const override {
        return errorMsg;
    }
    
    /**
     * Set calibration values
     * @param dry ADC value when sensor is in air/dry soil
     * @param wet ADC value when sensor is in saturated soil/water
     */
    void setCalibration(uint16_t dry, uint16_t wet) {
        dryValue = dry;
        wetValue = wet;
        DEBUG_PRINTF("[SEN0193] Calibration updated - Dry: %d, Wet: %d\n", 
                     dryValue, wetValue);
    }
    
    /**
     * Convert raw ADC value to percentage (0-100%)
     * @param rawValue Raw ADC reading
     * @return Moisture percentage (0% = dry, 100% = wet)
     */
    float rawToPercentage(uint16_t rawValue) {
        // Map from calibrated range to 0-100%
        // Note: Some sensors may have inverted relationship
        // Adjust the mapping based on your specific sensor behavior
        
        if (rawValue <= dryValue) return 0.0;
        if (rawValue >= wetValue) return 100.0;
        
        float percentage = 100.0 * (float)(rawValue - dryValue) / 
                          (float)(wetValue - dryValue);
        
        return constrain(percentage, 0.0, 100.0);
    }
    
    /**
     * Get current moisture as percentage
     * @return Moisture percentage or NaN if read fails
     */
    float getMoisturePercent() {
        if (!initialized) return NAN;
        
        uint16_t raw = readRawADC();
        return rawToPercentage(raw);
    }
    
private:
    /**
     * Read ADC with averaging to reduce noise
     * ESP32-C3 ADC is noisy, so we average multiple samples
     * @return Averaged ADC value (0-4095)
     */
    uint16_t readRawADC() {
        uint32_t sum = 0;
        
        // Read multiple samples
        for (int i = 0; i < SOIL_MOISTURE_SAMPLES; i++) {
            sum += analogRead(adcPin);
            delay(1); // Small delay between readings
        }
        
        // Return average
        return sum / SOIL_MOISTURE_SAMPLES;
    }
};

#endif // SEN0193_SENSOR_H