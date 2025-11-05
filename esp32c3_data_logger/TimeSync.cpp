#ifndef TIME_SYNC_CPP
#define TIME_SYNC_CPP

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "Config.h"
#include "DS1308Sensor.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// External sensor reference (defined in SensorTask.cpp)
extern DS1308Sensor* getRTCSensor();

// System status
extern SystemStatus systemStatus;

// Last sync time
time_t lastSyncTime = 0;
bool initialSyncComplete = false;

// ============================================================================
// NTP SYNCHRONIZATION
// ============================================================================

/**
 * Connect to WiFi for time synchronization
 * @return true if successful
 */
bool connectForTimeSync() {
    DEBUG_PRINTLN("[TIME_SYNC] Connecting to WiFi for NTP...");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_TIMEOUT_MS) {
            DEBUG_PRINTLN("[TIME_SYNC] WiFi connection timeout!");
            return false;
        }
        delay(500);
        DEBUG_PRINT(".");
    }
    
    DEBUG_PRINTLN(" Connected!");
    DEBUG_PRINTF("[TIME_SYNC] IP: %s\n", WiFi.localIP().toString().c_str());
    
    return true;
}

/**
 * Synchronize system time with NTP server
 * @return true if successful
 */
bool syncTimeWithNTP() {
    DEBUG_PRINTF("[TIME_SYNC] Contacting NTP server: %s\n", NTP_SERVER);
    
    // Configure NTP (UTC time)
    configTime(NTP_TIMEZONE_OFFSET, NTP_DAYLIGHT_OFFSET, NTP_SERVER);
    
    // Wait for time to be set
    int retry = 0;
    const int maxRetries = 20;
    struct tm timeinfo;
    
    while (!getLocalTime(&timeinfo) && retry < maxRetries) {
        DEBUG_PRINT(".");
        delay(500);
        retry++;
    }
    
    if (retry >= maxRetries) {
        DEBUG_PRINTLN("\n[TIME_SYNC] ERROR: Failed to get time from NTP!");
        return false;
    }
    
    DEBUG_PRINTLN("");
    
    // Get current time as Unix timestamp
    time_t now;
    time(&now);
    
    DEBUG_PRINTF("[TIME_SYNC] NTP time: %s", ctime(&now));
    DEBUG_PRINTF("[TIME_SYNC] Unix timestamp: %ld\n", now);
    
    return true;
}

/**
 * Update RTC with NTP time
 * @return true if successful
 */
bool updateRTCFromNTP() {
    DS1308Sensor* rtc = getRTCSensor();
    
    if (rtc == nullptr) {
        DEBUG_PRINTLN("[TIME_SYNC] ERROR: RTC sensor not available!");
        return false;
    }
    
    // Get current system time (already set from NTP)
    time_t now;
    time(&now);
    
    // Validate time is reasonable (after 2020)
    if (now < 1577836800) { // 2020-01-01 00:00:00 UTC
        DEBUG_PRINTLN("[TIME_SYNC] ERROR: NTP time is invalid!");
        return false;
    }
    
    // Update RTC
    if (!rtc->setTime(now)) {
        DEBUG_PRINTLN("[TIME_SYNC] ERROR: Failed to set RTC time!");
        return false;
    }
    
    // Verify RTC was set correctly
    time_t rtcTime = rtc->getUnixTime();
    int32_t timeDiff = abs(rtcTime - now);
    
    if (timeDiff > 2) { // Allow 2 second tolerance
        DEBUG_PRINTF("[TIME_SYNC] WARNING: RTC time differs by %ld seconds!\n", 
                    timeDiff);
        return false;
    }
    
    DEBUG_PRINTLN("[TIME_SYNC] RTC successfully synchronized with NTP");
    DEBUG_PRINTF("[TIME_SYNC] RTC time: %s\n", rtc->getTimeString().c_str());
    
    lastSyncTime = now;
    initialSyncComplete = true;
    
    return true;
}

/**
 * Perform complete time synchronization
 * Connects to WiFi, gets NTP time, updates RTC, disconnects
 * @return true if successful
 */
bool performTimeSync() {
    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTLN("[TIME_SYNC] Starting time synchronization");
    DEBUG_PRINTLN("========================================");
    
    bool success = false;
    
    // Step 1: Connect to WiFi
    if (!connectForTimeSync()) {
        DEBUG_PRINTLN("[TIME_SYNC] Failed: WiFi connection");
        goto cleanup;
    }
    
    // Step 2: Get time from NTP
    if (!syncTimeWithNTP()) {
        DEBUG_PRINTLN("[TIME_SYNC] Failed: NTP synchronization");
        goto cleanup;
    }
    
    // Step 3: Update RTC
    if (!updateRTCFromNTP()) {
        DEBUG_PRINTLN("[TIME_SYNC] Failed: RTC update");
        goto cleanup;
    }
    
    success = true;
    DEBUG_PRINTLN("[TIME_SYNC] ✓ Time synchronization successful!");
    
cleanup:
    // Always disconnect WiFi to save power
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    systemStatus.wifi_connected = false;
    
    DEBUG_PRINTLN("========================================\n");
    
    return success;
}

// ============================================================================
// TIME SYNCHRONIZATION TASK
// ============================================================================

/**
 * Very low-priority FreeRTOS task for periodic time synchronization
 * 
 * This task:
 * 1. Runs at startup to set initial RTC time
 * 2. Wakes up daily to resync with NTP
 * 3. Corrects clock drift over time
 */
void timeSyncTask(void* parameter) {
    DEBUG_PRINTLN("[TIME_SYNC] Task started");
    
    // ========================================================================
    // Initial time synchronization (critical)
    // ========================================================================
    delay(5000); // Wait for system to stabilize
    
    DEBUG_PRINTLN("[TIME_SYNC] Performing initial time synchronization...");
    
    int attempts = 0;
    const int maxAttempts = 3;
    
    while (!initialSyncComplete && attempts < maxAttempts) {
        attempts++;
        DEBUG_PRINTF("[TIME_SYNC] Attempt %d/%d\n", attempts, maxAttempts);
        
        if (performTimeSync()) {
            break;
        }
        
        if (attempts < maxAttempts) {
            DEBUG_PRINTLN("[TIME_SYNC] Retrying in 30 seconds...");
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }
    
    if (!initialSyncComplete) {
        DEBUG_PRINTLN("[TIME_SYNC] WARNING: Initial sync failed!");
        DEBUG_PRINTLN("[TIME_SYNC] System will use RTC time (may be inaccurate)");
    }
    
    // ========================================================================
    // Periodic synchronization (daily)
    // ========================================================================
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(TIME_SYNC_INTERVAL_MS);
    
    uint32_t syncCycles = 0;
    
    while (true) {
        // Wait for sync interval (24 hours by default)
        vTaskDelayUntil(&lastWakeTime, frequency);
        
        syncCycles++;
        DEBUG_PRINTF("[TIME_SYNC] Daily sync cycle %lu\n", syncCycles);
        
        // Perform time synchronization
        if (performTimeSync()) {
            DEBUG_PRINTLN("[TIME_SYNC] Daily sync successful");
        } else {
            DEBUG_PRINTLN("[TIME_SYNC] Daily sync failed (will retry tomorrow)");
        }
    }
    
    // Task should never exit
    DEBUG_PRINTLN("[TIME_SYNC] Task ending (unexpected!)");
    vTaskDelete(NULL);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Check if initial time synchronization is complete
 * @return true if RTC has been synced with NTP
 */
bool isTimeSynced() {
    return initialSyncComplete;
}

/**
 * Get time since last successful sync
 * @return Seconds since last sync, or 0 if never synced
 */
uint32_t getTimeSinceLastSync() {
    if (lastSyncTime == 0) return 0;
    
    time_t now;
    time(&now);
    return (uint32_t)(now - lastSyncTime);
}

/**
 * Manually trigger a time synchronization (for testing)
 * @return true if successful
 */
bool manualTimeSync() {
    return performTimeSync();
}

/**
 * Get last sync time as string
 * @return Human-readable last sync time
 */
String getLastSyncTimeString() {
    if (lastSyncTime == 0) {
        return "Never";
    }
    
    char buffer[32];
    struct tm* timeinfo = gmtime(&lastSyncTime);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", timeinfo);
    return String(buffer);
}

#endif // TIME_SYNC_CPP