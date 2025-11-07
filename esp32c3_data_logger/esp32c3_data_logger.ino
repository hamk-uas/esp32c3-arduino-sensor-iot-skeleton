#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
#include <Wire.h>
#include <RTClib.h>

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
// time_zone is configured in Secrets.h
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
 
// Serial monitor
#define SERIAL_BAUD_RATE 115200

// RTC
RTC_DS1307 rtc;

void syncRTCFromESP32(time_t esp32Time) {
  // Convert ESP32 time_t (UTC) to DateTime object
  DateTime dt = DateTime(esp32Time);
  
  Serial.println("\n--- Syncing RTC from ESP32 ---");
  Serial.print("ESP32 UTC time: ");
  printUTCTimeISO(esp32Time);
  
  // Set the RTC to UTC time
  rtc.adjust(dt);
  
  Serial.println("RTC has been synchronized to ESP32 UTC time");
  
  // Verify the sync
  DateTime rtcNow = rtc.now();
  time_t rtcTime = rtcNow.unixtime();
  Serial.print("RTC time after sync: ");
  printUTCTimeISO(rtcTime);
  Serial.println("-------------------------------\n");
}

void printRTCTimeISO() {
  if (!rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    return;
  }
  
  DateTime now = rtc.now();
  time_t rtcTime = now.unixtime();
  Serial.print("DS1308 ");
  printUTCTimeISO(rtcTime);
}

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

    // Initialize I2C for RTC
    Serial.print("Initializing I2C (SDA=");
    Serial.print(I2C_SDA_PIN);
    Serial.print(", SCL=");
    Serial.print(I2C_SCL_PIN);
    Serial.print(") ...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.println(" DONE");

    // Initialize RTC
    Serial.print("Initializing DS1307 RTC ...");
    if (!rtc.begin()) {
        Serial.println(" FAILED!");
        Serial.println("ERROR: Couldn't find RTC. Check wiring!");
        while (1) delay(10);
    }
    Serial.println(" DONE");

    // Check if RTC is running
    if (!rtc.isrunning()) {
        Serial.println("WARNING: RTC is NOT running. Will sync from NTP.");
    } else {
        Serial.println("RTC is running.");
        printRTCTimeISO();
    }

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
    Serial.print("ESP32  ");
    printUTCTimeISO(now);
    Serial.print("ESP32  ");
    printLocalTime(now);

    // Sync RTC with ESP32 time (UTC)
    now = time(nullptr);
    syncRTCFromESP32(now);
}

void loop() {
    digitalWrite(ledPin, HIGH); 
    Serial.println("LED OFF");
    delay(500);                     
    digitalWrite(ledPin, LOW); 
    Serial.println("LED ON");
    delay(500);

    // Print ESP32 time
    time_t now = time(nullptr);
    Serial.print("ESP32  ");
    printUTCTimeISO(now);
    
    // Print RTC time
    printRTCTimeISO();
}
