#ifndef CLOUD_TASK_CPP
#define CLOUD_TASK_CPP

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "Config.h"
#include "RawReading.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// FreeRTOS resources
extern QueueHandle_t aggregatedDataQueue;

// System status
extern SystemStatus systemStatus;

// MQTT client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Connection state
bool wifiConnected = false;
bool mqttConnected = false;

// ============================================================================
// WIFI CONNECTION
// ============================================================================

/**
 * Connect to WiFi network
 * @return true if successful, false on timeout
 */
bool connectToWiFi() {
    DEBUG_PRINTF("[CLOUD_TASK] Connecting to WiFi: %s\n", WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_TIMEOUT_MS) {
            DEBUG_PRINTLN("[CLOUD_TASK] WiFi connection timeout!");
            return false;
        }
        delay(500);
        DEBUG_PRINT(".");
    }
    
    DEBUG_PRINTLN("");
    DEBUG_PRINTF("[CLOUD_TASK] WiFi connected! IP: %s\n", 
                WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("[CLOUD_TASK] RSSI: %d dBm\n", WiFi.RSSI());
    
    return true;
}

/**
 * Disconnect from WiFi to save power
 */
void disconnectWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiConnected = false;
    DEBUG_PRINTLN("[CLOUD_TASK] WiFi disconnected");
}

// ============================================================================
// MQTT CONNECTION
// ============================================================================

/**
 * Connect to MQTT broker
 * @return true if successful, false otherwise
 */
bool connectToMQTT() {
    DEBUG_PRINTF("[CLOUD_TASK] Connecting to MQTT broker: %s:%d\n", 
                MQTT_BROKER, MQTT_PORT);
    
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    
    // Attempt connection
    bool connected;
    if (strlen(MQTT_USERNAME) > 0) {
        connected = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    } else {
        connected = mqttClient.connect(MQTT_CLIENT_ID);
    }
    
    if (connected) {
        DEBUG_PRINTLN("[CLOUD_TASK] MQTT connected!");
        return true;
    } else {
        DEBUG_PRINTF("[CLOUD_TASK] MQTT connection failed, rc=%d\n", 
                    mqttClient.state());
        return false;
    }
}

// ============================================================================
// DATA UPLOAD
// ============================================================================

/**
 * Convert aggregated data to JSON and publish via MQTT
 * @param data AggregatedData structure
 * @return true if successful, false otherwise
 */
bool publishDataToMQTT(const AggregatedData& data) {
    // Build JSON payload
    String payload = "{";
    payload += "\"device\":\"" + String(MQTT_CLIENT_ID) + "\",";
    payload += "\"start\":" + String(data.start_timestamp) + ",";
    payload += "\"end\":" + String(data.end_timestamp) + ",";
    payload += "\"samples\":" + String(data.sample_count) + ",";
    
    // DS18B20 data
    payload += "\"ds18b20\":{";
    payload += "\"avg\":" + String(data.ds18b20_avg, 2) + ",";
    payload += "\"min\":" + String(data.ds18b20_min, 2) + ",";
    payload += "\"max\":" + String(data.ds18b20_max, 2);
    payload += "},";
    
    // SHT40 Temperature
    payload += "\"sht40_temp\":{";
    payload += "\"avg\":" + String(data.sht40_temp_avg, 2) + ",";
    payload += "\"min\":" + String(data.sht40_temp_min, 2) + ",";
    payload += "\"max\":" + String(data.sht40_temp_max, 2);
    payload += "},";
    
    // SHT40 Humidity
    payload += "\"sht40_humidity\":{";
    payload += "\"avg\":" + String(data.sht40_hum_avg, 1) + ",";
    payload += "\"min\":" + String(data.sht40_hum_min, 1) + ",";
    payload += "\"max\":" + String(data.sht40_hum_max, 1);
    payload += "},";
    
    // Soil Moisture
    payload += "\"soil_moisture\":{";
    payload += "\"avg\":" + String(data.soil_moisture_avg, 0) + ",";
    payload += "\"min\":" + String(data.soil_moisture_min, 0) + ",";
    payload += "\"max\":" + String(data.soil_moisture_max, 0);
    payload += "}";
    
    payload += "}";
    
    // Publish to MQTT
    bool success = mqttClient.publish(MQTT_TOPIC, payload.c_str());
    
    if (success) {
        DEBUG_PRINTF("[CLOUD_TASK] Published %d bytes to %s\n", 
                    payload.length(), MQTT_TOPIC);
    } else {
        DEBUG_PRINTLN("[CLOUD_TASK] ERROR: MQTT publish failed!");
    }
    
    return success;
}

// ============================================================================
// CLOUD UPLOAD TASK
// ============================================================================

/**
 * Low-priority FreeRTOS task for uploading data to cloud
 * 
 * This task:
 * 1. Wakes up periodically (every 5 minutes)
 * 2. Connects to WiFi
 * 3. Connects to MQTT broker
 * 4. Uploads all pending aggregated data
 * 5. Disconnects to save power
 */
void cloudUploadTask(void* parameter) {
    DEBUG_PRINTLN("[CLOUD_TASK] Task started");
    
    if (!MQTT_ENABLED) {
        DEBUG_PRINTLN("[CLOUD_TASK] MQTT disabled in config, task suspending");
        vTaskSuspend(NULL); // Suspend task permanently
        return;
    }
    
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(CLOUD_UPLOAD_INTERVAL_MS);
    
    uint32_t uploadCycles = 0;
    uint32_t successfulUploads = 0;
    uint32_t failedUploads = 0;
    
    while (true) {
        uploadCycles++;
        DEBUG_PRINTF("[CLOUD_TASK] Upload cycle %lu starting\n", uploadCycles);
        
        // ====================================================================
        // STEP 1: Connect to WiFi
        // ====================================================================
        wifiConnected = connectToWiFi();
        systemStatus.wifi_connected = wifiConnected;
        
        if (!wifiConnected) {
            DEBUG_PRINTLN("[CLOUD_TASK] WiFi connection failed, skipping upload");
            vTaskDelayUntil(&lastWakeTime, frequency);
            continue;
        }
        
        // ====================================================================
        // STEP 2: Connect to MQTT
        // ====================================================================
        mqttConnected = connectToMQTT();
        systemStatus.mqtt_connected = mqttConnected;
        
        if (!mqttConnected) {
            DEBUG_PRINTLN("[CLOUD_TASK] MQTT connection failed");
            disconnectWiFi();
            failedUploads++;
            systemStatus.mqtt_errors++;
            vTaskDelayUntil(&lastWakeTime, frequency);
            continue;
        }
        
        // ====================================================================
        // STEP 3: Upload all pending data
        // ====================================================================
        AggregatedData data;
        uint32_t itemsUploaded = 0;
        
        // Process all items in queue (non-blocking)
        while (xQueueReceive(aggregatedDataQueue, &data, 0) == pdTRUE) {
            if (publishDataToMQTT(data)) {
                itemsUploaded++;
                successfulUploads++;
            } else {
                failedUploads++;
                systemStatus.mqtt_errors++;
                // Return failed item to queue
                xQueueSendToFront(aggregatedDataQueue, &data, 0);
                break; // Stop trying if publish fails
            }
            
            // Keep MQTT connection alive
            mqttClient.loop();
            delay(100); // Small delay between publishes
        }
        
        DEBUG_PRINTF("[CLOUD_TASK] Uploaded %lu items\n", itemsUploaded);
        
        // ====================================================================
        // STEP 4: Disconnect to save power
        // ====================================================================
        mqttClient.disconnect();
        mqttConnected = false;
        systemStatus.mqtt_connected = false;
        
        disconnectWiFi();
        
        DEBUG_PRINTF("[CLOUD_TASK] Upload cycle complete. Success: %lu, Failed: %lu\n",
                   successfulUploads, failedUploads);
        
        // ====================================================================
        // STEP 5: Wait until next upload window
        // ====================================================================
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
    
    // Task should never exit
    DEBUG_PRINTLN("[CLOUD_TASK] Task ending (unexpected!)");
    vTaskDelete(NULL);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Check WiFi connection status
 * @return true if connected
 */
bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

/**
 * Check MQTT connection status
 * @return true if connected
 */
bool isMQTTConnected() {
    return mqttClient.connected();
}

/**
 * Get WiFi signal strength
 * @return RSSI in dBm
 */
int getWiFiRSSI() {
    return WiFi.RSSI();
}

/**
 * Manually trigger an upload cycle (for testing)
 */
void triggerCloudUpload() {
    // This would require a notification mechanism
    // Placeholder for future implementation
    DEBUG_PRINTLN("[CLOUD_TASK] Manual upload triggered");
}

#endif // CLOUD_TASK_CPP