#ifndef ISENSOR_H
#define ISENSOR_H

#include <Arduino.h>
#include "RawReading.h"

// ============================================================================
// ABSTRACT SENSOR INTERFACE
// ============================================================================
// All sensor drivers must implement this interface to be compatible with
// the modular sensor reading task. This allows for plug-and-play sensor
// architecture where new sensors can be added without modifying core logic.

class ISensor {
public:
    // Virtual destructor ensures proper cleanup of derived classes
    virtual ~ISensor() = default;
    
    // ========================================================================
    // REQUIRED INTERFACE METHODS (Pure Virtual)
    // ========================================================================
    
    /**
     * Initialize the sensor hardware and communication interface.
     * 
     * @return true if initialization successful, false otherwise
     * 
     * This method should:
     * - Initialize communication bus (I2C, SPI, 1-Wire, etc.)
     * - Verify sensor presence and communication
     * - Configure sensor settings (resolution, sampling rate, etc.)
     * - Return false if sensor is not detected or configuration fails
     */
    virtual bool begin() = 0;
    
    /**
     * Read current sensor value(s) and populate the RawReading structure.
     * 
     * @param data Reference to RawReading struct to populate with sensor data
     * @return true if reading successful, false if sensor error or timeout
     * 
     * This method should:
     * - Request measurement from sensor (if applicable)
     * - Wait for conversion to complete
     * - Read the sensor value(s)
     * - Populate appropriate field(s) in the RawReading struct
     * - Return false if communication error or invalid reading
     * - NOT modify timestamp field (handled by sensor task)
     */
    virtual bool read(RawReading& data) = 0;
    
    /**
     * Get human-readable name of the sensor for logging and debugging.
     * 
     * @return Pointer to null-terminated string containing sensor name
     */
    virtual const char* getName() const = 0;
    
    // ========================================================================
    // OPTIONAL INTERFACE METHODS (Virtual with default implementation)
    // ========================================================================
    
    /**
     * Check if sensor is currently available and responding.
     * Default implementation always returns true.
     * 
     * @return true if sensor is healthy, false if communication error
     */
    virtual bool isAvailable() {
        return true;
    }
    
    /**
     * Reset the sensor to default state.
     * Default implementation does nothing.
     * 
     * @return true if reset successful, false otherwise
     */
    virtual bool reset() {
        return true;
    }
    
    /**
     * Get the expected measurement time in milliseconds.
     * Used for task timing optimization.
     * Default implementation returns 0 (instantaneous).
     * 
     * @return Measurement time in milliseconds
     */
    virtual uint32_t getMeasurementTimeMs() const {
        return 0;
    }
    
    /**
     * Get sensor-specific error message.
     * Default implementation returns empty string.
     * 
     * @return Pointer to error message string
     */
    virtual const char* getLastError() const {
        return "";
    }
};

#endif // ISENSOR_H