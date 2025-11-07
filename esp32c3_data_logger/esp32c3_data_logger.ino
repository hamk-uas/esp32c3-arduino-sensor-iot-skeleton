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
const char *time_zone = "EET-2EEST,M3.5.0/3,M10.5.0/4"; // See https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv

// Debugging
#define DEBUG_ENABLED true
#define SERIAL_BAUD_RATE 115200

void printUTCTimeISO(time_t &now) {
  struct tm timeinfo;
  char isoTime[30];
  gmtime_r(&now, &timeinfo);
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  Serial.println(isoTime);
}

void printLocalTime(time_t &now) {
  struct tm timeinfo;
  char isoTime[30];
  localtime_r(&now, &timeinfo);
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%S (local)", &timeinfo);
  Serial.println(isoTime);
}

void setup() {
    // Setup LED pin as output
    pinMode(ledPin, OUTPUT);

    // Setup serial for serial monitor
    Serial.begin(SERIAL_BAUD_RATE);

    // Print program name
    Serial.println("========================================");
    Serial.println("  ESP32-C3 Data Logger");
    Serial.println("========================================");

    // Scan for available WiFi hotspots
    Serial.print("Scanning WiFi ...");
    int n = WiFi.scanNetworks();
    Serial.println(" DONE");
    bool found_configured_ssid = false;
    for (int i = 0; i < n; i++) {
        bool is_configured_ssid = (strcmp(WiFi.SSID(i).c_str(), wifi_ssid) == 0);
        found_configured_ssid = found_configured_ssid || is_configured_ssid;
        Serial.printf("%d: %s  (%d dBm)  %s%s\n",
            i,
            WiFi.SSID(i).c_str(),
            WiFi.RSSI(i),
            (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED",
            (is_configured_ssid) ? "  Matches the configured SSID": ""
        );
    }
    if (!found_configured_ssid) {
        Serial.println("Warning: Configured WiFi SSID not found in scan.");
    }

    // Connect to the configured WiFi hotspot
    Serial.printf("\nConnecting to %s ...", wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" CONNECTED");

    // Get current time from a Network Time Protocol (NTP) server and set time zone to UTC
    Serial.print("Waiting for time sync ...");
    time_t now = 0;
    configTzTime(time_zone, ntpServer1, ntpServer2);
    while (now == 0) {
      Serial.print(".");
      delay(500);
      now = time(nullptr);
    }
    Serial.println(" DONE");
    printUTCTimeISO(now);
    printLocalTime(now);
}

void loop() {
    digitalWrite(ledPin, HIGH); 
    Serial.println("LED OFF");
    delay(1000);                     
    digitalWrite(ledPin, LOW); 
    Serial.println("LED ON");
    delay(1000);
    time_t now;
    now = time(nullptr);
    printUTCTimeISO(now);
}
