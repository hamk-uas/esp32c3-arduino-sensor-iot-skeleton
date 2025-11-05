#ifndef SENSOR_TASK_CPP
#define SENSOR_TASK_CPP

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "RawReading.h"
#include "ISensor.h"
#include "DS1308Sensor.h"
#include "DS18B20Sensor.h"
#include "SHT40Sensor.h"
#include "SEN0193Sensor.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// FreeRTOS Queue for passing raw readings to aggregation task
extern QueueHandle_t rawReadingQueue;

// RTC sensor instance (special sensor for time stamping)
DS1308Sensor* rtcSensor = nullptr;

// Vector of active sensor instances
std::vector<ISensor*> activeSensors;

// System statistics
extern SystemStatus systemStatus;

// ============================================================================
// SENSOR INITIALIZATION
// ============================================================================

/**
 * Initialize all sensors and add them to the active sensors list
 * Called once during setup
 */
bool initializeSensors() {
    DEBUG_PRINTLN("\n[SENSOR_TASK] Initializing all sensors...");
    
    bool allSuccess = true;
    
    // Initialize I2C bus for RTC and SHT40
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    DEBUG_PRINTLN("[SENSOR_TASK] I2C bus initialized");
    
    // 1. Initialize RTC (DS1308) - CRITICAL for time stamping
    rtcSensor = new DS1308Sensor();
    if (!rtcSensor->begin()) {
        DEBUG_PRINTLN("[SENSOR_TASK] CRITICAL: RTC initialization failed!");
        allSuccess = false;
    }
    
    // 2. Initialize DS18B20 (1-Wire Temperature)
    DS18B20Sensor* ds18b20 = new DS18B20Sensor(ONEWIRE_PIN);
    if (ds18b20->begin()) {
        activeSensors.push_back(ds18b20);
        DEBUG_PRINTLN("[SENSOR_TASK] DS18B20 added to active sensors");
    } else {
        DEBUG_PRINTLN("[SENSOR_TASK] WARNING: DS18B20 initialization failed");
        delete ds18b20;
        allSuccess = false;
    }
    
    // 3. Initialize SHT40 (I2C Temperature + Humidity)
    SHT40Sensor* sht40 = new SHT40Sensor();
    if (sht40->begin()) {
        activeSensors.push_back(sht40);
        DEBUG_PRINTLN("[SENSOR_TASK] SHT40 added to active sensors");
    } else {
        DEBUG_PRINTLN("[SENSOR_TASK] WARNING: SHT40 initialization failed");
        delete sht40;
        allSuccess = false;
    }
    
    // 4. Initialize SEN0193 (Analog Soil Moisture)
    SEN0193Sensor* soilMoisture = new SEN0193Sensor(SOIL_MOISTURE_PIN);
    if (soilMoisture->begin()) {
        activeSensors.push_back(soilMoisture);
        DEBUG_PRINTLN("[SENSOR_TASK] SEN0193 added to active sensors");
    } else {
        DEBUG_PRINTLN("[SENSOR_TASK] WARNING: SEN0193 initialization failed");
        delete soilMoisture;
        allSuccess = false;
    }
    
    DEBUG_PRINTF("[SENSOR_TASK] Initialization complete. Active sensors: %d\n", 
                 activeSensors.size());
    
    return allSuccess && (rtcSensor != nullptr);
}

// ============================================================================
// SENSOR READING TASK
// ============================================================================

/**
 * High-priority FreeRTOS task for reading all sensors at 1 Hz
 * 
 * This task:
 * 1. Reads timestamp from RTC (DS1308)
 * 2. Reads all environmental sensors
 * 3. Packages data into RawReading structure
 * 4. Sends to aggregation queue
 * 5. Maintains precise 1 Hz timing using vTaskDelayUntil
 */
void sensorReadingTask(void* parameter) {
    DEBUG_PRINTLN("[SENSOR_TASK] Task started");
    
    // Initialize all sensors
    if (!initializeSensors()) {
        DEBUG_PRINTLN("[SENSOR_TASK] CRITICAL: Sensor initialization failed!");
        // Continue anyway - some sensors may still work
    }
    
    // Timing variables for precise 1 Hz operation
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS);
    
    uint32_t successCount = 0;
    uint32_t failureCount = 0;
    
    // Main sensor reading loop
    while (true) {
        // Create new reading structure
        RawReading reading;
        bool readingValid = true;
        
        // ====================================================================
        // STEP 1: Get timestamp from RTC (CRITICAL)
        // ====================================================================
        if (rtcSensor != nullptr && rtcSensor->read(reading)) {
            // Timestamp successfully obtained
        } else {
            DEBUG_PRINTLN("[SENSOR_TASK] ERROR: Failed to read RTC timestamp!");
            reading.timestamp = 0; // Invalid timestamp
            readingValid = false;
            failureCount++;
        }
        
        // ====================================================================
        // STEP 2: Read all environmental sensors
        // ====================================================================
        for (ISensor* sensor : activeSensors) {
            if (!sensor->read(reading)) {
                DEBUG_PRINTF("[SENSOR_TASK] WARNING: %s read failed: %s\n",
                           sensor->getName(), sensor->getLastError());
                // Continue reading other sensors even if one fails
            }
        }
        
        // ====================================================================
        // STEP 3: Send reading to aggregation queue
        // ====================================================================
        if (readingValid) {
            if (xQueueSend(rawReadingQueue, &reading, 0) == pdTRUE) {
                successCount++;
                systemStatus.total_readings++;
                
                // Debug output every 10 readings
                if (successCount % 10 == 0) {
                    DEBUG_PRINTF("[SENSOR_TASK] Readings: %lu success, %lu failed\n",
                               successCount, failureCount);
                }
            } else {
                DEBUG_PRINTLN("[SENSOR_TASK] ERROR: Queue full! Data lost.");
                failureCount++;
                systemStatus.failed_readings++;
            }
        } else {
            failureCount++;
            systemStatus.failed_readings++;
        }
        
        // ====================================================================
        // STEP 4: Maintain precise 1 Hz timing
        // ====================================================================
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
    
    // Task should never exit, but clean up if it does
    DEBUG_PRINTLN("[SENSOR_TASK] Task ending (unexpected!)");
    vTaskDelete(NULL);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Get RTC sensor instance for time synchronization
 * @return Pointer to DS1308Sensor instance or nullptr if not initialized
 */
DS1308Sensor* getRTCSensor() {
    return rtcSensor;
}

/**
 * Print current sensor readings to serial for debugging
 */
void printCurrentReadings() {
    if (rtcSensor) {
        DEBUG_PRINTF("Time: %s UTC\n", rtcSensor->getTimeString().c_str());
    }
    
    for (ISensor* sensor : activeSensors) {
        RawReading dummy;
        if (sensor->read(dummy)) {
            DEBUG_PRINTF("%s: OK\n", sensor->getName());
        } else {
            DEBUG_PRINTF("%s: FAILED - %s\n", 
                        sensor->getName(), sensor->getLastError());
        }
    }
}

/**
 * Get count of active sensors
 * @return Number of successfully initialized sensors
 */
int getActiveSensorCount() {
    return activeSensors.size();
}

#endif // SENSOR_TASK_CPP