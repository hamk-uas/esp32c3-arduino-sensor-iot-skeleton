#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
#include <Wire.h>
#include <RTClib.h>

// Secrets
#include "Secrets.h"

// Configuration
// -------------

// I2C Pins (DS1308 RTC)
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;

// Serial monitor
constexpr unsigned long SERIAL_BAUD_RATE = 115200;

// WiFi
// wifi_ssid and wifi_password are configured in Secrets.h

// NTP server
// time_zone is configured in Secrets.h
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
 
// Contants
// --------

// Conversion factors
constexpr unsigned long uS_TO_S = 1000000ULL;  // Conversion factor for micro seconds to seconds

// State
// -----

// DS1308 RTC
RTC_DS1307 ds1308_rtc;  // DS1308 is compatible with DS1307

// Boot count in ESP32-C3 RTC memory, value retained over deep sleep
RTC_DATA_ATTR unsigned long boot_count = 0;

// Planned wake time (no plan initially, fill with zeros)
RTC_DATA_ATTR struct timeval wake_time = {0, 0};

// Functions
// ---------

// Calculate microseconds until next even minute (UTC)
unsigned long long microsecondsUntilNextEvenMinute(const struct timeval &tv) {
  struct tm timeinfo;
  gmtime_r(&tv.tv_sec, &timeinfo);
  
  // Get current seconds past the minute
  unsigned long seconds_past_minute = timeinfo.tm_sec;
  
  // Get current microseconds
  unsigned long microseconds = tv.tv_usec;
  
  // Calculate microseconds remaining in current minute
  unsigned long long microseconds_to_next_minute = 
    (60ULL - seconds_past_minute) * uS_TO_S - microseconds;
  
  // Check if current minute is odd
  bool current_minute_is_odd = (timeinfo.tm_min % 2) == 1;
  
  // If current minute is odd, we need to wait until the next minute
  // If current minute is even, we need to wait until the minute after next
  unsigned long long total_microseconds;
  if (current_minute_is_odd) {
    total_microseconds = microseconds_to_next_minute;
  } else {
    total_microseconds = microseconds_to_next_minute + (60ULL * uS_TO_S);
  }
  
  return total_microseconds;
}

// Sync DS1308 RTC time from ESP32
void syncDS1308RTCFromESP32() {
  // Get ESP32 time
  time_t esp32_now;
  esp32_now = time(nullptr);              

  // Convert ESP32 time_t (UTC) to DateTime object  
  DateTime esp32_dt = DateTime(esp32_now);

  // Set the RTC time
  ds1308_rtc.adjust(esp32_dt);                     
}

// Sync ESP32 time from DS1308 RTC
void syncESP32FromDS1308RTC() {
  // Get RTC time
  DateTime rtc_dt = ds1308_rtc.now();
  time_t rtc_now = rtc_dt.unixtime();

  // Set ESP32 system time from DS1308 RTC
  struct timeval tv;
  tv.tv_sec = rtc_now;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL); // Set full datetime
}

void printUTCTimeISOWithMicros(const struct timeval &tv) {
  struct tm timeinfo;
  char isoTime[40];
  gmtime_r(&tv.tv_sec, &timeinfo);
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  Serial.printf("%s.%06ldZ\n", isoTime, tv.tv_usec);
}

void printUTCTimeISO(const time_t &now) {
  struct tm timeinfo;
  char isoTime[30];
  gmtime_r(&now, &timeinfo);
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  Serial.println(isoTime);
}

void printLocalTime(const time_t &now) {
  struct tm timeinfo;
  char isoTime[30];
  localtime_r(&now, &timeinfo);
  strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%S (local)", &timeinfo);
  Serial.println(isoTime);
}

// Arduino setup and loop functions
// --------------------------------

void setup() {
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
    Serial.print("Initializing DS1308 RTC ...");
    if (!ds1308_rtc.begin()) {
        Serial.println(" FAILED!");
        Serial.println("ERROR: Couldn't find RTC. Check wiring!");
        while (1) delay(10);
    } else {
        while(!ds1308_rtc.isrunning()) {
          Serial.print(".");
          delay(1000);
        }
    }
    Serial.println(" DONE");
    DateTime rtc_dt = ds1308_rtc.now();
    time_t rtc_now = rtc_dt.unixtime();
    Serial.print("DS1308 RTC ");
    printUTCTimeISO(rtc_now);

    // Scan for available WiFi hotspots and check if the configured SSID is among them
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
    Serial.println(" DONE");
    Serial.print("My local IP address is ");
    Serial.println(WiFi.localIP());

    // Get ESP32 and DS1308 RTC time from Internet upon first boot, otherwise get time from DS1308 RTC
    if (boot_count == 0) {
      Serial.println("First boot, let's set the time from Internet!");
      // Get current time from a Network Time Protocol (NTP) server and set time zone
      Serial.print("Waiting for time sync ...");
      time_t esp32_now = 0;
      configTzTime(time_zone, ntpServer1, ntpServer2);
      while (esp32_now == 0) {
        Serial.print(".");
        delay(500);
        esp32_now = time(nullptr);
      }
      Serial.println(" DONE");
      Serial.println("Time after sync from NTP server:");
      Serial.print("ESP32      ");
      printUTCTimeISO(esp32_now);
      Serial.print("ESP32      ");
      printLocalTime(esp32_now);

      // Sync DS1308 RTC with ESP32 UTC time
      Serial.print("\nSyncing DS1308 RTC from ESP32 ...");
      syncDS1308RTCFromESP32();
      Serial.println(" DONE");
    } else {
      Serial.println("It's not the first boot, DS1308 RTC has been keeping time.");
      // Sync ESP32 UTC time with DS1308 RTC
      // This only has 1 second accuracy and might not be needed if you just use DS1308 RTC time
      Serial.print("\nSyncing ESP32 time from DS1308 RTC ...");
      syncESP32FromDS1308RTC();
      Serial.println(" DONE");
      // Print nominal sensing time
      Serial.print("Nominal timestamp ");
      printUTCTimeISOWithMicros(wake_time);
    }

    // Print ESP32 time
    {
      time_t esp32_now = time(nullptr);
      Serial.print("ESP32      ");
      printUTCTimeISO(esp32_now);
      Serial.print("           ");
      printLocalTime(esp32_now);
    }
    
    // Print RTC time
    {
      DateTime rtc_dt = ds1308_rtc.now();
      time_t rtc_now = rtc_dt.unixtime();
      Serial.print("DS1308 RTC ");
      printUTCTimeISO(rtc_now);
      Serial.print("           ");
      printLocalTime(rtc_now);
    }

    // Get current time with microsecond resolution
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    // Calculate sleep duration to wake at next even minute (UTC) with microsecond precision
    unsigned long long sleep_microseconds = microsecondsUntilNextEvenMinute(current_time);
    Serial.println("\n--- Deep Sleep Configuration ---");
    Serial.print("Current UTC time: ");
    printUTCTimeISOWithMicros(current_time);
    Serial.printf("Sleep duration: %llu microseconds (%.6f seconds)\n", 
                  sleep_microseconds, 
                  sleep_microseconds / 1000000.0);
    
    // Calculate and print expected wake time
    unsigned long long total_microseconds = 
      (unsigned long long)current_time.tv_sec * uS_TO_S + 
      current_time.tv_usec + 
      sleep_microseconds;
    wake_time.tv_sec = total_microseconds / uS_TO_S;
    wake_time.tv_usec = total_microseconds % uS_TO_S;
    Serial.print("Expected wake time (UTC): ");
    printUTCTimeISOWithMicros(wake_time);

    // Go to deep sleep for 5 seconds
    boot_count++;  // Increment boot count before deep sleep
    Serial.flush();  // Flush serial monitor
    esp_sleep_enable_timer_wakeup(sleep_microseconds);  // Configure wake-up timer (time in microseconds)
    esp_deep_sleep_start();  // Enter deep sleep
}

void loop() {
    // Not used because in setup() we go to deep sleep and after waking up setup() is run again
}
