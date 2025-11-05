#ifndef AGGREGATION_TASK_CPP
#define AGGREGATION_TASK_CPP

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "RawReading.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// FreeRTOS Queues
extern QueueHandle_t rawReadingQueue;
extern QueueHandle_t aggregatedDataQueue;

// ============================================================================
// STATISTICS HELPER CLASS
// ============================================================================

/**
 * Helper class for calculating running statistics (min, max, avg)
 */
class RunningStats {
private:
    float minVal;
    float maxVal;
    float sum;
    uint32_t count;
    
public:
    RunningStats() : minVal(INFINITY), maxVal(-INFINITY), sum(0.0), count(0) {}
    
    void reset() {
        minVal = INFINITY;
        maxVal = -INFINITY;
        sum = 0.0;
        count = 0;
    }
    
    void addValue(float value) {
        if (!isnan(value)) {
            if (value < minVal) minVal = value;
            if (value > maxVal) maxVal = value;
            sum += value;
            count++;
        }
    }
    
    float getMin() const { return (count > 0) ? minVal : NAN; }
    float getMax() const { return (count > 0) ? maxVal : NAN; }
    float getAvg() const { return (count > 0) ? (sum / count) : NAN; }
    uint32_t getCount() const { return count; }
};

// ============================================================================
// DATA AGGREGATION TASK
// ============================================================================

/**
 * Medium-priority FreeRTOS task for aggregating sensor data
 * 
 * This task:
 * 1. Collects raw readings from the sensor task queue
 * 2. Calculates statistics (min, max, avg) over aggregation window
 * 3. Packages aggregated data
 * 4. Sends to logging and cloud upload queues
 */
void aggregationTask(void* parameter) {
    DEBUG_PRINTLN("[AGGREGATION_TASK] Task started");
    
    // Statistics accumulators for each sensor
    RunningStats ds18b20Stats;
    RunningStats sht40TempStats;
    RunningStats sht40HumStats;
    RunningStats soilMoistureStats;
    
    // Timing variables
    time_t windowStartTime = 0;
    time_t windowEndTime = 0;
    
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(AGGREGATION_INTERVAL_MS);
    
    uint32_t aggregationCycles = 0;
    
    // Buffer for collecting readings during aggregation window
    std::vector<RawReading> readingBuffer;
    readingBuffer.reserve(RAW_READING_QUEUE_SIZE);
    
    while (true) {
        // ====================================================================
        // STEP 1: Collect all available raw readings from queue
        // ====================================================================
        RawReading rawReading;
        uint32_t readingsCollected = 0;
        
        // Process all readings currently in queue (non-blocking)
        while (xQueueReceive(rawReadingQueue, &rawReading, 0) == pdTRUE) {
            // Track time window
            if (windowStartTime == 0) {
                windowStartTime = rawReading.timestamp;
            }
            windowEndTime = rawReading.timestamp;
            
            // Add to statistics
            ds18b20Stats.addValue(rawReading.ds18b20_temp);
            sht40TempStats.addValue(rawReading.sht40_temp);
            sht40HumStats.addValue(rawReading.sht40_humidity);
            soilMoistureStats.addValue(rawReading.sen0193_moisture_raw);
            
            readingsCollected++;
        }
        
        // ====================================================================
        // STEP 2: Wait until aggregation window completes
        // ====================================================================
        if (readingsCollected > 0) {
            DEBUG_PRINTF("[AGGREGATION_TASK] Collected %lu readings\n", readingsCollected);
            
            // Create aggregated data structure
            AggregatedData aggData;
            aggData.start_timestamp = windowStartTime;
            aggData.end_timestamp = windowEndTime;
            aggData.sample_count = readingsCollected;
            
            // DS18B20 statistics
            aggData.ds18b20_avg = ds18b20Stats.getAvg();
            aggData.ds18b20_min = ds18b20Stats.getMin();
            aggData.ds18b20_max = ds18b20Stats.getMax();
            
            // SHT40 Temperature statistics
            aggData.sht40_temp_avg = sht40TempStats.getAvg();
            aggData.sht40_temp_min = sht40TempStats.getMin();
            aggData.sht40_temp_max = sht40TempStats.getMax();
            
            // SHT40 Humidity statistics
            aggData.sht40_hum_avg = sht40HumStats.getAvg();
            aggData.sht40_hum_min = sht40HumStats.getMin();
            aggData.sht40_hum_max = sht40HumStats.getMax();
            
            // Soil Moisture statistics
            aggData.soil_moisture_avg = soilMoistureStats.getAvg();
            aggData.soil_moisture_min = soilMoistureStats.getMin();
            aggData.soil_moisture_max = soilMoistureStats.getMax();
            
            // ================================================================
            // STEP 3: Send aggregated data to output queues
            // ================================================================
            
            // Send to logging queue (for SD card)
            if (xQueueSend(aggregatedDataQueue, &aggData, pdMS_TO_TICKS(1000)) != pdTRUE) {
                DEBUG_PRINTLN("[AGGREGATION_TASK] ERROR: Failed to send to queue!");
            } else {
                aggregationCycles++;
                
                // Debug output
                DEBUG_PRINTLN("[AGGREGATION_TASK] --- Aggregated Data ---");
                DEBUG_PRINTF("  Window: %ld to %ld (%d samples)\n", 
                           aggData.start_timestamp, aggData.end_timestamp, 
                           aggData.sample_count);
                DEBUG_PRINTF("  DS18B20: %.2f°C (%.2f - %.2f)\n",
                           aggData.ds18b20_avg, aggData.ds18b20_min, 
                           aggData.ds18b20_max);
                DEBUG_PRINTF("  SHT40 Temp: %.2f°C (%.2f - %.2f)\n",
                           aggData.sht40_temp_avg, aggData.sht40_temp_min,
                           aggData.sht40_temp_max);
                DEBUG_PRINTF("  SHT40 RH: %.1f%% (%.1f - %.1f)\n",
                           aggData.sht40_hum_avg, aggData.sht40_hum_min,
                           aggData.sht40_hum_max);
                DEBUG_PRINTF("  Soil: %.0f ADC (%.0f - %.0f)\n",
                           aggData.soil_moisture_avg, aggData.soil_moisture_min,
                           aggData.soil_moisture_max);
            }
            
            // ================================================================
            // STEP 4: Reset statistics for next window
            // ================================================================
            ds18b20Stats.reset();
            sht40TempStats.reset();
            sht40HumStats.reset();
            soilMoistureStats.reset();
            windowStartTime = 0;
            windowEndTime = 0;
            
        } else {
            DEBUG_PRINTLN("[AGGREGATION_TASK] No readings to aggregate");
        }
        
        // ====================================================================
        // STEP 5: Wait for next aggregation cycle
        // ====================================================================
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
    
    // Task should never exit
    DEBUG_PRINTLN("[AGGREGATION_TASK] Task ending (unexpected!)");
    vTaskDelete(NULL);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Get aggregated data formatted as JSON string
 * @param data AggregatedData structure
 * @return JSON string
 */
String aggregatedDataToJSON(const AggregatedData& data) {
    String json = "{";
    json += "\"start\":\"" + String(data.start_timestamp) + "\",";
    json += "\"end\":\"" + String(data.end_timestamp) + "\",";
    json += "\"samples\":" + String(data.sample_count) + ",";
    json += "\"ds18b20\":{";
    json += "\"avg\":" + String(data.ds18b20_avg, 2) + ",";
    json += "\"min\":" + String(data.ds18b20_min, 2) + ",";
    json += "\"max\":" + String(data.ds18b20_max, 2);
    json += "},";
    json += "\"sht40_temp\":{";
    json += "\"avg\":" + String(data.sht40_temp_avg, 2) + ",";
    json += "\"min\":" + String(data.sht40_temp_min, 2) + ",";
    json += "\"max\":" + String(data.sht40_temp_max, 2);
    json += "},";
    json += "\"sht40_hum\":{";
    json += "\"avg\":" + String(data.sht40_hum_avg, 1) + ",";
    json += "\"min\":" + String(data.sht40_hum_min, 1) + ",";
    json += "\"max\":" + String(data.sht40_hum_max, 1);
    json += "},";
    json += "\"soil\":{";
    json += "\"avg\":" + String(data.soil_moisture_avg, 0) + ",";
    json += "\"min\":" + String(data.soil_moisture_min, 0) + ",";
    json += "\"max\":" + String(data.soil_moisture_max, 0);
    json += "}";
    json += "}";
    return json;
}

#endif // AGGREGATION_TASK_CPP