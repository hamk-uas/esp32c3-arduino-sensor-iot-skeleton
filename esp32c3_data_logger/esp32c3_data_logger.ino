/**
 * ESP32-C3 Modular Data Logger
 * 
 * A robust, FreeRTOS-based multi-sensor data logging system with:
 * - Modular sensor architecture (easy to add new sensors)
 * - Real-time clock with NTP synchronization
 * - SD card logging with thread-safe file access
 * - MQTT cloud upload capability
 * - Proper task prioritization and timing
 * 
 * Author: [Your Name]
 * Date: 2024
 * License: MIT
 */

#include <Arduino.h>
#include <Wire.h>
#include "Config.h"
#include "RawReading.h"

// ============================================================================
// FREERTOS RESOURCES
// ============================================================================

// Task Handles
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t aggregationTaskHandle = NULL;
TaskHandle_t loggingTaskHandle = NULL;
TaskHandle_t cloudTaskHandle = NULL;
TaskHandle_t timeSyncTaskHandle = NULL;

// Queue Handles
QueueHandle_t rawReadingQueue = NULL;
QueueHandle_t aggregatedDataQueue = NULL;

// Mutex Handle
SemaphoreHandle_t sdCardMutex = NULL;

// ============================================================================
// SYSTEM STATUS
// ============================================================================

SystemStatus systemStatus;

// ============================================================================
// TASK FUNCTION DECLARATIONS
// ============================================================================

// Defined in respective .cpp files
extern void sensorReadingTask(void* parameter);
extern void aggregationTask(void* parameter);
extern void loggingTask(void* parameter);
extern void cloudUploadTask(void* parameter);
extern void timeSyncTask(void* parameter);

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
    // ========================================================================
    // 1. INITIALIZE SERIAL COMMUNICATION
    // ========================================================================
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000); // Wait for serial to stabilize
    
    DEBUG_PRINTLN("\n\n");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("  ESP32-C3 Modular Data Logger");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("");
    
    // Print compile-time information
    DEBUG_PRINTF("Compiled: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTF("Arduino Core: %s\n", ARDUINO_ESP32_RELEASE);
    DEBUG_PRINTF("Free Heap: %lu bytes\n", ESP.getFreeHeap());
    DEBUG_PRINTF("Chip Model: %s\n", ESP.getChipModel());
    DEBUG_PRINTF("CPU Frequency: %lu MHz\n", ESP.getCpuFreqMHz());
    DEBUG_PRINTLN("");
    
    // ========================================================================
    // 2. CREATE FREERTOS RESOURCES
    // ========================================================================
    DEBUG_PRINTLN("[SETUP] Creating FreeRTOS resources...");
    
    // Create queues
    rawReadingQueue = xQueueCreate(RAW_READING_QUEUE_SIZE, sizeof(RawReading));
    if (rawReadingQueue == NULL) {
        DEBUG_PRINTLN("[SETUP] FATAL: Failed to create raw reading queue!");
        while (1) delay(1000);
    }
    DEBUG_PRINTLN("[SETUP] ✓ Raw reading queue created");
    
    aggregatedDataQueue = xQueueCreate(AGGREGATED_DATA_QUEUE_SIZE, sizeof(AggregatedData));
    if (aggregatedDataQueue == NULL) {
        DEBUG_PRINTLN("[SETUP] FATAL: Failed to create aggregated data queue!");
        while (1) delay(1000);
    }
    DEBUG_PRINTLN("[SETUP] ✓ Aggregated data queue created");
    
    // Create mutex for SD card access
    sdCardMutex = xSemaphoreCreateMutex();
    if (sdCardMutex == NULL) {
        DEBUG_PRINTLN("[SETUP] FATAL: Failed to create SD card mutex!");
        while (1) delay(1000);
    }
    DEBUG_PRINTLN("[SETUP] ✓ SD card mutex created");
    
    DEBUG_PRINTLN("");
    
    // ========================================================================
    // 3. CREATE FREERTOS TASKS
    // ========================================================================
    DEBUG_PRINTLN("[SETUP] Creating FreeRTOS tasks...");
    
    // Time Synchronization Task (Lowest priority - runs at startup and daily)
    xTaskCreate(
        timeSyncTask,           // Function
        "TimeSync",             // Name
        TIME_SYNC_TASK_STACK,   // Stack size
        NULL,                   // Parameters
        TIME_SYNC_TASK_PRIORITY,// Priority
        &timeSyncTaskHandle     // Handle
    );
    DEBUG_PRINTLN("[SETUP] ✓ Time Sync task created (Priority 0)");
    
    // Sensor Reading Task (Highest priority - time critical)
    xTaskCreate(
        sensorReadingTask,      // Function
        "SensorRead",           // Name
        SENSOR_TASK_STACK,      // Stack size
        NULL,                   // Parameters
        SENSOR_TASK_PRIORITY,   // Priority
        &sensorTaskHandle      // Handle
    );
    DEBUG_PRINTLN("[SETUP] ✓ Sensor Reading task created (Priority 3)");
    
    // Data Aggregation Task (Medium priority)
    xTaskCreate(
        aggregationTask,        // Function
        "Aggregation",          // Name
        AGGREGATION_TASK_STACK, // Stack size
        NULL,                   // Parameters
        AGGREGATION_TASK_PRIORITY, // Priority
        &aggregationTaskHandle  // Handle
    );
    DEBUG_PRINTLN("[SETUP] ✓ Aggregation task created (Priority 2)");
    
    // SD Logging Task (Medium priority)
    xTaskCreate(
        loggingTask,            // Function
        "SDLogging",            // Name
        LOGGING_TASK_STACK,     // Stack size
        NULL,                   // Parameters
        LOGGING_TASK_PRIORITY,  // Priority
        &loggingTaskHandle      // Handle
    );
    DEBUG_PRINTLN("[SETUP] ✓ SD Logging task created (Priority 2)");
    
    // Cloud Upload Task (Low priority)
    xTaskCreate(
        cloudUploadTask,        // Function
        "CloudUpload",          // Name
        CLOUD_TASK_STACK,       // Stack size
        NULL,                   // Parameters
        CLOUD_TASK_PRIORITY,    // Priority
        &cloudTaskHandle        // Handle
    );
    DEBUG_PRINTLN("[SETUP] ✓ Cloud Upload task created (Priority 1)");
    
    DEBUG_PRINTLN("");
    
    // ========================================================================
    // 4. SETUP COMPLETE
    // ========================================================================
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("  Setup Complete - System Running");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("Task priorities:");
    DEBUG_PRINTLN("  3: Sensor Reading (1s interval)");
    DEBUG_PRINTLN("  2: Aggregation (60s interval)");
    DEBUG_PRINTLN("  2: SD Logging (on-demand)");
    DEBUG_PRINTLN("  1: Cloud Upload (5min interval)");
    DEBUG_PRINTLN("  0: Time Sync (daily)");
    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("Serial commands:");
    DEBUG_PRINTLN("  's' - System status");
    DEBUG_PRINTLN("  't' - Current time");
    DEBUG_PRINTLN("  'r' - Sensor readings");
    DEBUG_PRINTLN("  'f' - SD card files");
    DEBUG_PRINTLN("  'h' - Heap info");
    DEBUG_PRINTLN("  'n' - Manual NTP sync");
    DEBUG_PRINTLN("");
}

// ============================================================================
// LOOP FUNCTION
// ============================================================================

/**
 * Main loop - runs as lowest priority FreeRTOS task
 * Used for non-critical serial monitoring and status updates
 */
void loop() {
    // Update system status
    systemStatus.uptime_seconds = millis() / 1000;
    systemStatus.free_heap = ESP.getFreeHeap();
    systemStatus.min_free_heap = ESP.getMinFreeHeap();
    
    // Check for serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        
        switch (cmd) {
            case 's': // System status
                printSystemStatus();
                break;
                
            case 't': // Current time
                printCurrentTime();
                break;
                
            case 'r': // Sensor readings
                printSensorReadings();
                break;
                
            case 'f': // SD files
                listSDFiles();
                break;
                
            case 'h': // Heap info
                printHeapInfo();
                break;
                
            case 'n': // Manual NTP sync
                DEBUG_PRINTLN("Triggering manual NTP sync...");
                manualTimeSync();
                break;
                
            case '\n':
            case '\r':
                // Ignore newlines
                break;
                
            default:
                DEBUG_PRINTF("Unknown command: %c\n", cmd);
                DEBUG_PRINTLN("Available commands: s, t, r, f, h, n");
                break;
        }
    }
    
    // Status LED heartbeat (if you have an LED connected)
    // pinMode(LED_BUILTIN, OUTPUT);
    // digitalWrite(LED_BUILTIN, (millis() / 1000) % 2);
    
    // Low-priority task - run infrequently
    delay(1000);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// External function declarations
extern void printCurrentReadings();
extern void listSDFiles();
extern void printSDStats();
extern bool isTimeSynced();
extern String getLastSyncTimeString();
extern DS1308Sensor* getRTCSensor();

/**
 * Print comprehensive system status
 */
void printSystemStatus() {
    DEBUG_PRINTLN("\n========== SYSTEM STATUS ==========");
    
    // Uptime
    uint32_t days = systemStatus.uptime_seconds / 86400;
    uint32_t hours = (systemStatus.uptime_seconds % 86400) / 3600;
    uint32_t minutes = (systemStatus.uptime_seconds % 3600) / 60;
    uint32_t seconds = systemStatus.uptime_seconds % 60;
    DEBUG_PRINTF("Uptime: %lud %02lu:%02lu:%02lu\n", days, hours, minutes, seconds);
    
    // Memory
    DEBUG_PRINTF("Free Heap: %lu bytes\n", systemStatus.free_heap);
    DEBUG_PRINTF("Min Free Heap: %lu bytes\n", systemStatus.min_free_heap);
    
    // Connectivity
    DEBUG_PRINTF("WiFi: %s\n", systemStatus.wifi_connected ? "Connected" : "Disconnected");
    DEBUG_PRINTF("MQTT: %s\n", systemStatus.mqtt_connected ? "Connected" : "Disconnected");
    DEBUG_PRINTF("SD Card: %s\n", systemStatus.sd_card_ok ? "OK" : "Error");
    
    // Statistics
    DEBUG_PRINTF("Total Readings: %lu\n", systemStatus.total_readings);
    DEBUG_PRINTF("Failed Readings: %lu\n", systemStatus.failed_readings);
    DEBUG_PRINTF("SD Write Errors: %lu\n", systemStatus.sd_write_errors);
    DEBUG_PRINTF("MQTT Errors: %lu\n", systemStatus.mqtt_errors);
    
    // Time sync
    DEBUG_PRINTF("Time Synced: %s\n", isTimeSynced() ? "Yes" : "No");
    DEBUG_PRINTF("Last Sync: %s\n", getLastSyncTimeString().c_str());
    
    DEBUG_PRINTLN("===================================\n");
}

/**
 * Print current RTC time
 */
void printCurrentTime() {
    DS1308Sensor* rtc = getRTCSensor();
    if (rtc) {
        DEBUG_PRINTF("\nCurrent Time: %s UTC\n", rtc->getTimeString().c_str());
        DEBUG_PRINTF("Unix Timestamp: %ld\n\n", rtc->getUnixTime());
    } else {
        DEBUG_PRINTLN("\nRTC not available\n");
    }
}

/**
 * Print current sensor readings
 */
void printSensorReadings() {
    DEBUG_PRINTLN("\n========== SENSOR READINGS ==========");
    printCurrentReadings();
    DEBUG_PRINTLN("=====================================\n");
}

/**
 * Print heap memory information
 */
void printHeapInfo() {
    DEBUG_PRINTLN("\n========== HEAP INFORMATION ==========");
    DEBUG_PRINTF("Free Heap: %lu bytes\n", ESP.getFreeHeap());
    DEBUG_PRINTF("Min Free Heap: %lu bytes\n", ESP.getMinFreeHeap());
    DEBUG_PRINTF("Heap Size: %lu bytes\n", ESP.getHeapSize());
    DEBUG_PRINTF("Max Alloc Heap: %lu bytes\n", ESP.getMaxAllocHeap());
    DEBUG_PRINTLN("======================================\n");
}