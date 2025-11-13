#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
#include <Wire.h>
#include <RTClib.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <WebServer.h>

// Secrets
// -------

// See README.md for instructions on creating this file
#include "Secrets.h"

// Helpful constants
// -----------------

// Conversion factor from microseconds to seconds
constexpr uint64_t MICROS_PER_SECOND = 1000000ULL;

// Operation mode enum
enum Mode {
  MODE_DATALOGGER,
  MODE_WEBSERVER
};

// Configuration
// -------------

// Sampling period in seconds. This should be long enough for all of the following. Otherwise dropouts may occur.
// * Sensor reading
// * WiFi connection establishment (~3 s)
// * Logging (SD card & cloud)
// * Time sync (~15 s)
constexpr uint64_t samplingPeriodSeconds = 30; // 30 seconds

// Sampling time adjustment
constexpr float adjustSleepSeconds = -0.008738f;

// Maximum RTC clock drift in ppm for the temperature range, for NTP sync scheduling
// See Fig. 3 of Maxim Integrated APPLICATION NOTE 504: Design Considerations for Maxim Real-Time Clocks, Feb 15, 2002
// https://www.mouser.com/pdfDocs/AN504-2.pdf
constexpr float rtcDriftPpm = 170; // 170 ppm for field conditions with a minimum temperature of -40 degrees C

// Allowed clock drift in seconds, for NTP sync scheduling
constexpr float allowedDriftSeconds = 0.1f;

// I2C Pins (DS1308 RTC)
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;

// Serial monitor
constexpr uint32_t SERIAL_BAUD_RATE = 115200;

// NTP server
const char* ntpServerPrimary = "pool.ntp.org";
const char* ntpServerSecondary = "time.nist.gov";

// Precalculated constants
// -----------------------

// Sampling period in microseconds
constexpr uint64_t samplingPeriodMicros = samplingPeriodSeconds * MICROS_PER_SECOND;

// NTP sync interval in microseconds
constexpr uint64_t ntpSyncIntervalMicros = (uint64_t)(MICROS_PER_SECOND * allowedDriftSeconds / (rtcDriftPpm/1e6f));

// NTP sync interval in sampling periods
static_assert(ntpSyncIntervalMicros >= samplingPeriodMicros, "NTP sync interval must be longer than the sampling period. Increase allowedDriftSeconds or reduce rtcDriftPpm.");
static_assert(ntpSyncIntervalMicros / samplingPeriodMicros <= UINT32_MAX, "NTP sync interval in sampling periods must fit in uint32_t. Decrease allowedDriftSeconds or increase rtcDriftPpm.");
constexpr uint32_t ntpSyncIntervalSamplingPeriods = (uint32_t)(ntpSyncIntervalMicros / samplingPeriodMicros);

// Web server port
const uint16_t SERVER_PORT = 80;

// State
// -----

// DS1308 RTC (compatible with DS1307)
RTC_DS1307 rtc;

// Boot count in ESP32-C3 RTC memory, value retained over deep sleep
RTC_DATA_ATTR uint32_t bootCount = 0;

// Statistics on sample time shifts (mean and root mean square)
RTC_DATA_ATTR float meanSampleShiftSeconds = 0;
RTC_DATA_ATTR uint32_t sampleCount = 0;
RTC_DATA_ATTR float meanSquareSampleShiftSeconds = 0;

// Planned wake time (no plan initially, fill with zeros)
RTC_DATA_ATTR struct timeval nominalWakeTime = {0, 0};

// Preferences (used for saving current mode)
Preferences prefs;

// Get current mode from preferences. Returns MODE_DATALOGGER if not set.
Mode getCurrentMode() {
  prefs.begin("mode", true); // read-only
  int val = prefs.getInt("currentMode", MODE_DATALOGGER);
  prefs.end();
  return (Mode)val;
}

// Set current mode in preferences
void setCurrentMode(Mode m) {
  prefs.begin("mode", false); // write
  prefs.putInt("currentMode", m);
  prefs.end();
}

// Current mode 
Mode currentMode = getCurrentMode();

// Web server
WebServer server(SERVER_PORT);

// ===== Serve the root page with file list =====
void handleRoot() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Files</title></head><body>");
  html += F("<h1>Files in LittleFS</h1><ul>");

  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String name = file.name();   // e.g. "/blah.csv"
    String displayName = name.substring(1); // remove leading '/'
    html += "<li><a href=\"/download?file=" + displayName + "\">" + displayName + "</a></li>";
    file = root.openNextFile();
  }

  html += F("</ul></body></html>");
  server.send(200, "text/html", html);
}

// ===== Serve individual CSV files =====
void handleDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file argument");
    return;
  }

  String fileName = "/" + server.arg("file");  // add back leading slash

  if (!LittleFS.exists(fileName)) {
    server.send(404, "text/plain", "File not found: " + fileName);
    return;
  }

  File f = LittleFS.open(fileName, "r");
  server.streamFile(f, "text/csv");
  f.close();
}

// Functions
// ---------

// Post sensor data to cloud via HTTP
bool httpPost(const char* timestamp, float temperature_esp32) {
  HTTPClient http;
  
  Serial.print("Posting to cloud ...");
  
  http.begin(thingspeak_api_url);
  http.addHeader("Content-Type", "application/json");
  
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"api_key\":\"%s\",\"created_at\":\"%s\",\"field1\":%.2f}",
           thingspeak_api_key, timestamp, temperature_esp32);
  
  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.printf(" DONE (HTTP %d, response: %s)\n", httpResponseCode, response.c_str());
    http.end();
    return true;
  } else {
    Serial.printf(" FAILED (Error: %s)\n", http.errorToString(httpResponseCode).c_str());
    http.end();
    return false;
  }
}

uint64_t microsecondsUntilNextSample(const struct timeval& now, uint64_t samplingPeriodMicros) {
  uint64_t nowMicros = (uint64_t)now.tv_sec * MICROS_PER_SECOND + now.tv_usec;
  uint64_t midnightMicros = (uint64_t)(now.tv_sec - (now.tv_sec % 86400UL)) * MICROS_PER_SECOND;
  uint64_t microsToday = nowMicros - midnightMicros;
  uint64_t nextSlotToday = ((microsToday / samplingPeriodMicros) + 1ULL) * samplingPeriodMicros;
  return midnightMicros + nextSlotToday - nowMicros;
}

// Sync DS1308 RTC time from ESP32 at next clean second boundary.
void syncRtcFromEsp32() {
  time_t t1 = time(nullptr);
  for(;;) {
    time_t t2 = time(nullptr);
    if (t2 != t1) {
      rtc.adjust(DateTime(t2));
      return;
    }
    delay(1);
  }  
}

// Sync ESP32 time from DS1308 RTC at a clean second boundary
void syncEsp32FromRtc() {
  DateTime t1 = rtc.now();
  for(;;) {
    DateTime t2 = rtc.now();
    if (t2.second() != t1.second()) {
      struct timeval tv = {t2.unixtime(), 0};
      settimeofday(&tv, nullptr);
      return;
    }
    delay(1);
  }
}

void formatTimeIso(time_t t, char* buf, size_t size, long usec = -1) {
  if (!buf || size < 21) return;
  struct tm timeinfo;
  gmtime_r(&t, &timeinfo);
  strftime(buf, size, "%Y-%m-%dT%H:%M:%S", &timeinfo);
  if (usec >= 0 && size >= 28) {
    char temp[28];
    snprintf(temp, sizeof(temp), "%s.%06ldZ", buf, usec);
    strncpy(buf, temp, size - 1);
    buf[size - 1] = '\0';
  } else {
    strncat(buf, "Z", size - strlen(buf) - 1);
  }
}

void getTimeString(char* buf, size_t size, bool useRtc) {
  if (!buf || size < 21) return;
  time_t now = useRtc ? rtc.now().unixtime() : time(nullptr);
  formatTimeIso(now, buf, size);
}

// Arduino setup() and loop()
// --------------------------

void setup() {
  // Read the sensor data first to minimize delays
  uint64_t espTimerAtSetupStart = esp_timer_get_time();
  float temperature_esp32 = temperatureRead();

  // Setup serial monitor
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial) delay(100);

  // Print program name
  Serial.println("============== ESP32-C3 Data Logger & Web Server==============");
  Serial.printf("Boot count: %" PRIu32 "\n", bootCount);

  // ===== Initialize LittleFS =====
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed!");
    while (1) delay(1000);
  }
 
  // Initialize RTC
  Serial.print("Initializing DS1308 RTC ...");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!rtc.begin()) {
    Serial.println(" FAILED!");
    Serial.println("ERROR: Couldn't find RTC. Check wiring!");
    while (1) delay(10);
  }
  while (!rtc.isrunning()) {
    Serial.print(".");
    delay(500);
  }
  char timeStr[40];
  getTimeString(timeStr, sizeof(timeStr), true);
  Serial.printf(" DONE, got time: %s\n", timeStr);

  // On the first boot, also scan for available WiFi hotspots for debugging purposes
  if (bootCount == 0) {
    Serial.print("Scanning WiFi ...");
    int networkCount = WiFi.scanNetworks();
    Serial.println(" DONE");

    bool foundConfiguredSsid = false;
    for (int i = 0; i < networkCount; i++) {
      bool isConfigured = (strcmp(WiFi.SSID(i).c_str(), wifi_ssid) == 0);
      foundConfiguredSsid |= isConfigured;
      Serial.printf("%d: %s  (%" PRIi32 " dBm)  %s%s\n",
        i, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
        (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED",
        isConfigured ? "  Matches the configured SSID" : "");
    }
    if (!foundConfiguredSsid) {
      Serial.println("Warning: Configured WiFi SSID not found in scan.");
    }
  }

  // Connect to the configured WiFi hotspot
  Serial.printf("WiFi connecting to %s ...", wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf(" DONE, got local ip %s\n", WiFi.localIP().toString().c_str());

  // Mode switching using serial command
  if (bootCount == 0) {
    Serial.println("Commands:");
    Serial.printf("l: Set mode to Data logger%s\n", (currentMode == MODE_DATALOGGER) ? " (current)" : "");
    Serial.printf("s: Set mode to Web server%s\n", (currentMode == MODE_WEBSERVER) ? " (current)" : "");
    Serial.print("Enter command within 5 seconds ...");
    for (int i = 0; i < 5; i++) {
      delay(1000);
      Serial.print(".");
      if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.equalsIgnoreCase("s")) {
          currentMode = MODE_WEBSERVER;
          setCurrentMode(currentMode);
          break;
        } else if (input.equalsIgnoreCase("l")) {
          currentMode = MODE_DATALOGGER;
          setCurrentMode(currentMode);
          break;
        } 
      }
    }
  }
  
  if (currentMode == MODE_DATALOGGER) {
    // Datalogger mode active
    Serial.println(" Activating Data logger!");

    // Get ESP32 and DS1308 RTC time from Internet per NTP sync schedule
    // or if not scheduled for this boot, get ESP32 time from DS1308 RTC
    if (bootCount % ntpSyncIntervalSamplingPeriods == 0) {
      // Sync ESP32 time from NTP
      Serial.print("Syncing time from NTP ...");
      time_t now = 0;
      struct tm timeinfo;
      configTzTime(time_zone, ntpServerPrimary, ntpServerSecondary);
      for(;;) {
          time(&now);
          gmtime_r(&now, &timeinfo);
          if (timeinfo.tm_year >= (2025 - 1900)) break;
          Serial.print(".");
          delay(500);
      }
      Serial.println(" DONE");
      
      // Sync DS1308 RTC from ESP32 UTC time
      Serial.print("Syncing DS1308 RTC from ESP32 ...");
      syncRtcFromEsp32();
      Serial.println(" DONE");
    } else {
      // Print time until next NTP sync
      uint32_t remainder = bootCount % ntpSyncIntervalSamplingPeriods;
      Serial.printf("Boots remaining until NTP sync: %" PRIu32 "\n", 
                    (remainder == 0) ? 0 : ntpSyncIntervalSamplingPeriods - remainder);

      // Sync ESP32 UTC time from DS1308 RTC
      Serial.print("Syncing ESP32 time from DS1308 RTC ...");
      syncEsp32FromRtc();
      Serial.println(" DONE");
    }

    // Calculate and print when setup() actually started running
    if (bootCount != 0) {
      asm volatile("":::"memory");
      struct timeval timeAtSetupStart;
      uint64_t espTimerAtSync = esp_timer_get_time();
      gettimeofday(&timeAtSetupStart, NULL);  // First get current time, then adjust backwards:
      timeAtSetupStart.tv_sec -= (espTimerAtSync - espTimerAtSetupStart) / MICROS_PER_SECOND;
      timeAtSetupStart.tv_usec -= (espTimerAtSync - espTimerAtSetupStart) % MICROS_PER_SECOND;
      if (timeAtSetupStart.tv_usec < 0) {
        timeAtSetupStart.tv_sec -= 1;
        timeAtSetupStart.tv_usec += MICROS_PER_SECOND;
      }
      char setupStartTimeStr[40];
      formatTimeIso(timeAtSetupStart.tv_sec, setupStartTimeStr, sizeof(setupStartTimeStr), timeAtSetupStart.tv_usec);
      Serial.printf("Setup start time (estimated): %s\n", setupStartTimeStr);
      float sampleShiftSeconds = (timeAtSetupStart.tv_sec - nominalWakeTime.tv_sec) + (timeAtSetupStart.tv_usec - nominalWakeTime.tv_usec) / 1e6f;
      sampleCount++;
      float delta_mean = (sampleShiftSeconds - meanSampleShiftSeconds) / sampleCount;
      meanSampleShiftSeconds = meanSampleShiftSeconds + delta_mean;
      float sampleShiftSecondsSquare = sampleShiftSeconds * sampleShiftSeconds;
      float delta_mean_sq = (sampleShiftSecondsSquare - meanSquareSampleShiftSeconds) / sampleCount;
      meanSquareSampleShiftSeconds = meanSquareSampleShiftSeconds + delta_mean_sq;
      float rmsSampleShiftSeconds = sqrtf(meanSquareSampleShiftSeconds);
      Serial.printf("Sample time shift from nominal (estimated): %.3f seconds (mean: %.3f, RMS: %.3f)\n", sampleShiftSeconds, meanSampleShiftSeconds, rmsSampleShiftSeconds);
    }

    // Log sensor data if not the first boot
    if (bootCount != 0) {
      // Get UTC timestamp
      char utcTimestampStrBuf[40];
      formatTimeIso(nominalWakeTime.tv_sec, utcTimestampStrBuf, sizeof(utcTimestampStrBuf), nominalWakeTime.tv_usec);

      // Print to serial
      while (!Serial) delay(100);
      Serial.println("-----------------data logging-----------------");
      Serial.println("time_utc,temperature_esp32");
      Serial.printf("%s,%f\n", utcTimestampStrBuf, temperature_esp32);
      Serial.println("----------------------------------------------");

      // Print to log file
      char logFileName[20];
      snprintf(logFileName, sizeof(logFileName), "/%.*s.csv", 7, utcTimestampStrBuf); // YYYY-MM.csv
      if (!LittleFS.exists(logFileName)) {
        File logFile = LittleFS.open(logFileName, "w");
        if (logFile) {
          logFile.println("time_utc,temperature_esp32");
          logFile.close();
        }
      }
      File logFile = LittleFS.open(logFileName, "a");
      if (logFile) {
        logFile.printf("%s,%f\n", utcTimestampStrBuf, temperature_esp32);
        logFile.close();
      } else {
        Serial.println("Failed to open log file");
      }
      Serial.printf("Compensated sample lag: %.6f seconds\n", espTimerAtSetupStart/1e6f + adjustSleepSeconds);

      // Post to cloud
      httpPost(utcTimestampStrBuf, temperature_esp32);
    }

    // Print time
    char rtcTime[40], esp32Time[40];
    struct timeval now;
    getTimeString(rtcTime, sizeof(rtcTime), true);
    gettimeofday(&now, NULL);
    formatTimeIso(now.tv_sec, esp32Time, sizeof(esp32Time), now.tv_usec);
    Serial.println("Current time:");
    Serial.printf("DS1308 RTC %s\n", rtcTime);
    Serial.printf("ESP32      %s\n", esp32Time);

    // Calculate deep sleep duration to wake at next sampling time
    struct timeval currentTime;
    gettimeofday(&currentTime, NULL);
    int64_t sleepMicros = microsecondsUntilNextSample(currentTime, samplingPeriodMicros);
    uint64_t totalMicros = (uint64_t)currentTime.tv_sec * MICROS_PER_SECOND + currentTime.tv_usec + sleepMicros;
    nominalWakeTime.tv_sec = totalMicros / MICROS_PER_SECOND;
    nominalWakeTime.tv_usec = totalMicros % MICROS_PER_SECOND;
    char wakeTime[40];
    formatTimeIso(nominalWakeTime.tv_sec, wakeTime, sizeof(wakeTime), nominalWakeTime.tv_usec);
    Serial.printf("Will sleep until %s\n", wakeTime);
    sleepMicros += adjustSleepSeconds * 1e6f;
    if (sleepMicros < 0) sleepMicros = 0;

    // Go to deep sleep
    bootCount++;
    LittleFS.end();
    Serial.flush();
    esp_sleep_enable_timer_wakeup(sleepMicros);
    esp_deep_sleep_start();
  } else {
    // Web server mode active
    Serial.println(" Activating Web server!");
    server.on("/", handleRoot);
    server.on("/download", handleDownload);
    server.begin();
    Serial.printf("Web Server running at http://%s:%u/\n", WiFi.localIP().toString().c_str(), SERVER_PORT);
  }
}

// Only used in web server mode
void loop() {
  // Handle web server clients
  server.handleClient();
}