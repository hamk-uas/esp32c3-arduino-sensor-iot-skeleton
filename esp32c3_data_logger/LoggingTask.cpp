#ifndef LOGGING_TASK_CPP
#define LOGGING_TASK_CPP

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include "Config.h"
#include "RawReading.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// FreeRTOS resources
extern QueueHandle_t aggregatedDataQueue;
extern SemaphoreHandle_t sdCardMutex;

// System status
extern SystemStatus systemStatus;

// SD card state
bool sdCardInitialized = false;
String currentLogFile = "";

// ============================================================================
// SD CARD INITIALIZATION
// ============================================================================

/**
 * Initialize SD card and SPI interface
 * @return true if successful, false otherwise
 */
bool initializeSDCard() {
    DEBUG_PRINTLN("[LOGGING_TASK] Initializing SD card...");
    
    // Initialize SPI bus
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    
    // Attempt to mount SD card
    if (!SD.begin(SD_CS_PIN)) {
        DEBUG_PRINTLN("[LOGGING_TASK] ERROR: SD card mount failed!");
        return false;
    }
    
    // Check card type
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        DEBUG_PRINTLN("[LOGGING_TASK] ERROR: No SD card attached!");
        return false;
    }
    
    // Print card info
    DEBUG_PRINT("[LOGGING_TASK] SD Card Type: ");
    if (cardType == CARD_MMC) {
        DEBUG_PRINTLN("MMC");
    } else if (cardType == CARD_SD) {
        DEBUG_PRINTLN("SDSC");
    } else if (cardType == CARD_SDHC) {
        DEBUG_PRINTLN("SDHC");
    } else {
        DEBUG_PRINTLN("UNKNOWN");
    }
    
    // Print card size
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    DEBUG_PRINTF("[LOGGING_TASK] SD Card Size: %llu MB\n", cardSize);
    
    DEBUG_PRINTLN("[LOGGING_TASK] SD card initialized successfully");
    return true;
}

// ============================================================================
// FILE MANAGEMENT
// ============================================================================

/**
 * Generate filename based on current date
 * Format: /data_YYYYMMDD.csv
 * @param timestamp Unix timestamp
 * @return Filename string
 */
String getLogFilename(time_t timestamp) {
    struct tm* timeinfo = gmtime(&timestamp);
    char filename[32];
    snprintf(filename, sizeof(filename), "%s%04d%02d%02d.csv",
            SD_FILENAME_PREFIX,
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday);
    return String(filename);
}

/**
 * Ensure CSV file exists with proper headers
 * @param filename File path
 * @return true if file is ready, false on error
 */
bool ensureFileWithHeaders(const String& filename) {
    // Check if file already exists
    if (SD.exists(filename)) {
        return true; // File exists, assume headers are present
    }
    
    // Create new file with headers
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        DEBUG_PRINTF("[LOGGING_TASK] ERROR: Failed to create file: %s\n", 
                    filename.c_str());
        return false;
    }
    
    // Write CSV headers
    file.println("timestamp_start,timestamp_end,samples,"
                "ds18b20_avg,ds18b20_min,ds18b20_max,"
                "sht40_temp_avg,sht40_temp_min,sht40_temp_max,"
                "sht40_hum_avg,sht40_hum_min,sht40_hum_max,"
                "soil_avg,soil_min,soil_max");
    
    file.close();
    
    DEBUG_PRINTF("[LOGGING_TASK] Created new log file: %s\n", filename.c_str());
    return true;
}

/**
 * Write aggregated data to CSV file
 * @param data AggregatedData structure
 * @return true if successful, false on error
 */
bool writeDataToSD(const AggregatedData& data) {
    // Get filename for current date
    String filename = getLogFilename(data.start_timestamp);
    
    // Ensure file exists with headers
    if (!ensureFileWithHeaders(filename)) {
        return false;
    }
    
    // Open file in append mode
    File file = SD.open(filename, FILE_APPEND);
    if (!file) {
        DEBUG_PRINTF("[LOGGING_TASK] ERROR: Failed to open file: %s\n", 
                    filename.c_str());
        return false;
    }
    
    // Write data as CSV row
    file.printf("%ld,%ld,%d,",
               data.start_timestamp, data.end_timestamp, data.sample_count);
    
    // DS18B20 data
    if (isnan(data.ds18b20_avg)) {
        file.print(",,,");
    } else {
        file.printf("%.2f,%.2f,%.2f,",
                   data.ds18b20_avg, data.ds18b20_min, data.ds18b20_max);
    }
    
    // SHT40 Temperature data
    if (isnan(data.sht40_temp_avg)) {
        file.print(",,,");
    } else {
        file.printf("%.2f,%.2f,%.2f,",
                   data.sht40_temp_avg, data.sht40_temp_min, data.sht40_temp_max);
    }
    
    // SHT40 Humidity data
    if (isnan(data.sht40_hum_avg)) {
        file.print(",,,");
    } else {
        file.printf("%.1f,%.1f,%.1f,",
                   data.sht40_hum_avg, data.sht40_hum_min, data.sht40_hum_max);
    }
    
    // Soil Moisture data
    if (isnan(data.soil_moisture_avg)) {
        file.print(",,");
    } else {
        file.printf("%.0f,%.0f,%.0f",
                   data.soil_moisture_avg, data.soil_moisture_min, 
                   data.soil_moisture_max);
    }
    
    file.println();
    file.close();
    
    return true;
}

// ============================================================================
// LOGGING TASK
// ============================================================================

/**
 * Medium-priority FreeRTOS task for writing data to SD card
 * 
 * This task:
 * 1. Waits for aggregated data from queue
 * 2. Acquires SD card mutex (thread-safe)
 * 3. Writes data to daily CSV file
 * 4. Handles file rotation at midnight
 */
void loggingTask(void* parameter) {
    DEBUG_PRINTLN("[LOGGING_TASK] Task started");
    
    // Initialize SD card
    sdCardInitialized = initializeSDCard();
    systemStatus.sd_card_ok = sdCardInitialized;
    
    if (!sdCardInitialized) {
        DEBUG_PRINTLN("[LOGGING_TASK] WARNING: SD card not available, logging disabled");
        // Don't exit - might recover if card is inserted later
    }
    
    uint32_t successfulWrites = 0;
    uint32_t failedWrites = 0;
    
    while (true) {
        AggregatedData data;
        
        // Wait for aggregated data (blocking)
        if (xQueueReceive(aggregatedDataQueue, &data, portMAX_DELAY) == pdTRUE) {
            
            if (!sdCardInitialized) {
                // Attempt to reinitialize SD card
                sdCardInitialized = initializeSDCard();
                systemStatus.sd_card_ok = sdCardInitialized;
                
                if (!sdCardInitialized) {
                    DEBUG_PRINTLN("[LOGGING_TASK] SD card still unavailable");
                    failedWrites++;
                    systemStatus.sd_write_errors++;
                    continue;
                }
            }
            
            // ================================================================
            // CRITICAL SECTION: SD card access (use mutex)
            // ================================================================
            if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                
                // Write data to SD card
                bool success = writeDataToSD(data);
                
                // Release mutex immediately after write
                xSemaphoreGive(sdCardMutex);
                
                // Update statistics
                if (success) {
                    successfulWrites++;
                    DEBUG_PRINTF("[LOGGING_TASK] Data written successfully (%lu total)\n",
                               successfulWrites);
                } else {
                    failedWrites++;
                    systemStatus.sd_write_errors++;
                    DEBUG_PRINTF("[LOGGING_TASK] Write failed (%lu failures)\n",
                               failedWrites);
                    
                    // Card might have been removed
                    sdCardInitialized = false;
                    systemStatus.sd_card_ok = false;
                }
                
            } else {
                DEBUG_PRINTLN("[LOGGING_TASK] ERROR: Failed to acquire SD mutex!");
                failedWrites++;
                systemStatus.sd_write_errors++;
            }
        }
    }
    
    // Task should never exit
    DEBUG_PRINTLN("[LOGGING_TASK] Task ending (unexpected!)");
    vTaskDelete(NULL);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * List all files on SD card
 */
void listSDFiles() {
    if (!sdCardInitialized) {
        DEBUG_PRINTLN("SD card not initialized");
        return;
    }
    
    File root = SD.open("/");
    if (!root) {
        DEBUG_PRINTLN("Failed to open root directory");
        return;
    }
    
    DEBUG_PRINTLN("\n=== SD Card Files ===");
    File file = root.openNextFile();
    while (file) {
        DEBUG_PRINTF("  %s - %lu bytes\n", file.name(), file.size());
        file = root.openNextFile();
    }
    DEBUG_PRINTLN("=====================\n");
}

/**
 * Get SD card usage statistics
 */
void printSDStats() {
    if (!sdCardInitialized) {
        DEBUG_PRINTLN("SD card not initialized");
        return;
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
    uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
    
    DEBUG_PRINTLN("\n=== SD Card Statistics ===");
    DEBUG_PRINTF("Card Size: %llu MB\n", cardSize);
    DEBUG_PRINTF("Total: %llu MB\n", totalBytes);
    DEBUG_PRINTF("Used: %llu MB\n", usedBytes);
    DEBUG_PRINTF("Free: %llu MB\n", totalBytes - usedBytes);
    DEBUG_PRINTLN("==========================\n");
}

#endif // LOGGING_TASK_CPP