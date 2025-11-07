//#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
// Secrets
#include "Secrets.h"

// ESP32 C3 Super Mini on-board LED (works with inverted logic)
const int ledPin = 8; 

// I2C Pins (DS1308 RTC)
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// WiFi
// wifi_ssid and wifi_password are configured in Secrets.h

// NTP server
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const char *time_zone = "UTC0";  // See https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h

// Debugging
#define DEBUG_ENABLED true
#define SERIAL_BAUD_RATE 115200

void printUTCTimeISO() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("No time available (yet)");
    return;
  }

  char isoTime[25];
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  Serial.println(isoTime);
}

void timeavailable(struct timeval *t) {
  Serial.println("Got time adjustment from NTP!");
  printUTCTimeISO();
}

void setup() {
    pinMode(ledPin, OUTPUT);
    Serial.begin(SERIAL_BAUD_RATE);

    Serial.println("========================================");
    Serial.println("  ESP32-C3 Data Logger");
    Serial.println("========================================");
    Serial.println("");
    Serial.printf("Compiled: %s %s\n", __DATE__, __TIME__);
    Serial.printf("Free Heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Chip Model: %s\n", ESP.getChipModel());
    Serial.printf("CPU Frequency: %lu MHz\n", ESP.getCpuFreqMHz());
    Serial.println("");    

    Serial.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    Serial.println("Scan done");
    for (int i = 0; i < n; i++) {
        Serial.printf("%d: %s  (%d dBm)  %s%s\n",
            i+1,
            WiFi.SSID(i).c_str(),
            WiFi.RSSI(i),
            (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED",
            (strcmp(WiFi.SSID(i).c_str(), wifi_ssid) == 0) ? "  Matches the configured SSID": ""
        );
    }

    Serial.printf("\nConnecting to %s ", wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    //esp_sntp_servermode_dhcp(1);  // (optional)
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" CONNECTED");
    // set notification call-back function
    sntp_set_time_sync_notification_cb(timeavailable);
    configTzTime(time_zone, ntpServer1, ntpServer2);
}

void loop() {
    digitalWrite(ledPin, HIGH); 
    Serial.println("LED OFF");
    delay(1000);                     
    digitalWrite(ledPin, LOW); 
    Serial.println("LED ON");
    delay(1000);        
}
