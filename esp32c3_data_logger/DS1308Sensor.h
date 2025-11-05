#ifndef DS1308_SENSOR_H
#define DS1308_SENSOR_H

#include "ISensor.h"
#include <RTClib.h>
#include "Config.h"

// ============================================================================
// DS1308 REAL-TIME CLOCK (RTC)
// ============================================================================
// Implements ISensor interface for DS1308 I2C RTC module.
// Provides battery-backed UTC time stamping independent of network.
// Note: This is a special "sensor" that provides timestamps rather than
// environmental measurements.

class DS1308Sensor : public ISensor {
private:
    RTC_DS1307 rtc; // RTClib uses DS1307 class for DS1308 (compatible)
    bool rtcFound;
    char errorMsg[64];
    DateTime lastReadTime;
    
public:
    /**
     * Constructor
     */
    DS1308Sensor() : rtcFound(false) {
        errorMsg[0] = '\0';
    }
    
    /**
     * Initialize DS1308 RTC
     * - Checks for RTC presence on I2C bus
     * - Verifies RTC is running
     * - Warns if time appears invalid
     */
    bool begin() override {
        DEBUG_PRINTLN("[DS1308] Initializing...");
        
        // Initialize RTC
        if (!rtc.begin()) {
            snprintf(errorMsg, sizeof(errorMsg), "DS1308 not found on I2C bus");
            DEBUG_PRINTLN("[DS1308] ERROR: Not found on I2C bus!");
            return false;
        }
        
        // Check if RTC is running
        if (!rtc.isrunning()) {
            snprintf(errorMsg, sizeof(errorMsg), "RTC not running - needs time set");
            DEBUG_PRINTLN("[DS1308] WARNING: RTC not running! Time needs to be set.");
            // Don't fail - we can still use it after NTP sync
        }
        
        // Read current time
        DateTime now = rtc.now();
        lastReadTime = now;
        
        // Check if time is reasonable (after 2020)
        if (now.year() < 2020) {
            DEBUG_PRINTF("[DS1308] WARNING: Time may be invalid: %04d-%02d-%02d %02d:%02d:%02d\n",
                        now.year(), now.month(), now.day(),
                        now.hour(), now.minute(), now.second());
            DEBUG_PRINTLN("[DS1308] Time will be synced with NTP");
        } else {
            DEBUG_PRINTF("[DS1308] Current time: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                        now.year(), now.month(), now.day(),
                        now.hour(), now.minute(), now.second());
        }
        
        rtcFound = true;
        return true;
    }
    
    /**
     * Read current time from RTC and populate timestamp
     * @param data RawReading structure to populate with timestamp
     * @return true if successful, false on error
     */
    bool read(RawReading& data) override {
        if (!rtcFound) {
            snprintf(errorMsg, sizeof(errorMsg), "RTC not initialized");
            return false;
        }
        
        // Read current time
        DateTime now = rtc.now();
        
        // Validate time is reasonable
        if (now.year() < 2020 || now.year() > 2100) {
            snprintf(errorMsg, sizeof(errorMsg), "Invalid year: %d", now.year());
            DEBUG_PRINTF("[DS1308] ERROR: Invalid year: %d\n", now.year());
            return false;
        }
        
        // Store timestamp as Unix epoch (UTC)
        data.timestamp = now.unixtime();
        lastReadTime = now;
        
        return true;
    }
    
    /**
     * Get sensor name for logging
     */
    const char* getName() const override {
        return "DS1308-RTC";
    }
    
    /**
     * Check if RTC is available and running
     */
    bool isAvailable() override {
        if (!rtcFound) return false;
        return rtc.isrunning();
    }
    
    /**
     * Get measurement time (I2C read is fast)
     */
    uint32_t getMeasurementTimeMs() const override {
        return 1; // I2C read is very fast
    }
    
    /**
     * Get last error message
     */
    const char* getLastError() const override {
        return errorMsg;
    }
    
    /**
     * Set RTC time from DateTime object
     * @param dt DateTime object with new time (should be UTC)
     * @return true if successful
     */
    bool setTime(const DateTime& dt) {
        if (!rtcFound) return false;
        
        rtc.adjust(dt);
        DEBUG_PRINTF("[DS1308] Time set to: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                    dt.year(), dt.month(), dt.day(),
                    dt.hour(), dt.minute(), dt.second());
        return true;
    }
    
    /**
     * Set RTC time from Unix timestamp
     * @param unixTime Unix epoch time (seconds since 1970-01-01 00:00:00 UTC)
     * @return true if successful
     */
    bool setTime(time_t unixTime) {
        if (!rtcFound) return false;
        
        DateTime dt = DateTime(unixTime);
        return setTime(dt);
    }
    
    /**
     * Get current time as DateTime object
     * @return Current DateTime
     */
    DateTime getDateTime() {
        if (!rtcFound) return DateTime(0);
        return rtc.now();
    }
    
    /**
     * Get current time as Unix timestamp
     * @return Unix epoch time
     */
    time_t getUnixTime() {
        if (!rtcFound) return 0;
        return rtc.now().unixtime();
    }
    
    /**
     * Get current time as formatted string
     * @return Time string in ISO 8601 format: YYYY-MM-DD HH:MM:SS
     */
    String getTimeString() {
        if (!rtcFound) return "RTC not available";
        
        DateTime now = rtc.now();
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
        return String(buffer);
    }
    
    /**
     * Check if RTC has lost power (time reset to 2000-01-01)
     * @return true if power was lost
     */
    bool hasLostPower() {
        if (!rtcFound) return true;
        
        DateTime now = rtc.now();
        return (now.year() == 2000 && now.month() == 1 && now.day() == 1);
    }
    
    /**
     * Get underlying RTC object for advanced operations
     * @return Reference to RTClib RTC_DS1307 object
     */
    RTC_DS1307& getRTC() {
        return rtc;
    }
};

#endif // DS1308_SENSOR_H