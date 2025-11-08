#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
#include <Wire.h>
#include <RTClib.h>

// Secrets
// -------

// See README.md for instructions on creating this file
#include "Secrets.h"

// Helpful constants
// -----------------

// Conversion factor from microseconds to seconds
constexpr uint64_t MICROS_PER_SECOND = 1000000ULL;

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

// State
// -----

// DS1308 RTC (compatible with DS1307)
RTC_DS1307 rtc;

// Boot count in ESP32-C3 RTC memory, value retained over deep sleep
RTC_DATA_ATTR uint32_t bootCount = 0;

// Planned wake time (no plan initially, fill with zeros)
RTC_DATA_ATTR struct timeval nominalWakeTime = {0, 0};

// Functions
// ---------

uint64_t microsecondsUntilNextSample(
    const struct timeval& now,
    uint64_t samplingPeriodMicros
) {
  // 1) Convert now to epoch microseconds
  uint64_t nowMicros = (uint64_t)now.tv_sec * MICROS_PER_SECOND + now.tv_usec;

  // 2) Find today's midnight (UTC) in epoch micros
  uint64_t midnightMicros = (uint64_t)(now.tv_sec - (now.tv_sec % 86400UL)) * MICROS_PER_SECOND;

  // 3) Compute how many microseconds have elapsed since midnight
  uint64_t microsToday = nowMicros - midnightMicros;

  // 4) Compute next sample slot offset from midnight
  uint64_t nextSlotToday = ((microsToday / samplingPeriodMicros) + 1ULL) * samplingPeriodMicros;

  // 5) Compute absolute target timestamp (may naturally roll into next day)
  uint64_t targetMicros = midnightMicros + nextSlotToday;

  // 6) Return the wait time from now
  return targetMicros - nowMicros;
}

// Sync DS1308 RTC time from ESP32 at next clean second boundary.
void syncRtcFromEsp32() {
  // 1. Get initial time
  time_t t1 = time(nullptr);

  // 2. Wait for the second to change
  // DS1308 will reset the countdown chain (32678 Hz -> 1 Hz divider flipflops)
  // at the moment of setting the time so we do it exactly at a second rollover.
  for(;;) {
    time_t t2 = time(nullptr);
    if (t2 != t1) {
      // 3. t2 is the first moment of the new second → sync RTC now
      rtc.adjust(DateTime(t2));
      return;
    }
    delay(1);  // 1 ms polling. This doesn't need to be very efficient as we only do this on NTP sync.
  }  
}

// Sync ESP32 time from DS1308 RTC at a clean second boundary
void syncEsp32FromRtc() {
  // 1. Read initial RTC time
  DateTime t1 = rtc.now();

  // 2. Wait for the RTC second to change
  for(;;) {
    DateTime t2 = rtc.now();
    if (t2.second() != t1.second()) {
      // 3. Sync ESP32 clock immediately after rollover
      struct timeval tv;
      tv.tv_sec  = t2.unixtime();
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);
      return;
    }
    delay(1);  // 1 ms polling. We need to poll as RTC time can only be seen at 1s resolution
  }
}

void formatUtcTimeIsoMicros(const struct timeval& tv, char* buf, size_t bufSize) {
  struct tm timeinfo;
  char tempBuf[40];
  gmtime_r(&tv.tv_sec, &timeinfo);
  strftime(tempBuf, sizeof(tempBuf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  snprintf(buf, bufSize, "%s.%06ldZ", tempBuf, tv.tv_usec);
}

void formatUtcTimeIso(const time_t& now, char* buf, size_t bufSize) {
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
}

void formatLocalTime(const time_t& now, char* buf, size_t bufSize) {
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  strftime(buf, bufSize, "%Y-%m-%dT%H:%M:%S (local)", &timeinfo);
}

void getEsp32UtcTimeString(char* buf, size_t bufSize) {
  time_t now = time(nullptr);
  formatUtcTimeIso(now, buf, bufSize);
}

void getRtcUtcTimeString(char* buf, size_t bufSize) {
  DateTime rtcDt = rtc.now();
  time_t rtcNow = rtcDt.unixtime();
  formatUtcTimeIso(rtcNow, buf, bufSize);
}

// Arduino setup() and loop()
// --------------------------

void setup() {
  // Read the sensor data first to minimize delays
  uint64_t sensorReadLagMicros = esp_timer_get_time(); // microseconds since boot
  float temperature = temperatureRead(); // ESP32 internal temperature sensor

  // Setup serial monitor
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial) {
    delay(100);
  }

  // Print program name
  Serial.println("==============================================");
  Serial.printf("ESP32-C3 Data Logger (bootCount = %" PRIu32 ")\n", bootCount);
  Serial.println("==============================================");

  // Initialize RTC and print RTC time if not the first boot
  Serial.print("Initializing DS1308 RTC ...");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!rtc.begin()) {
    Serial.println(" FAILED!");
    Serial.println("ERROR: Couldn't find RTC. Check wiring!");
    while (1) delay(10);
  } else {
    while (!rtc.isrunning()) {
      Serial.print(".");
      delay(500);
    }
  }
  char timeStr[40];
  getRtcUtcTimeString(timeStr, sizeof(timeStr));
  Serial.print(" DONE, got time: ");  
  Serial.println(timeStr);

  // Log sensor data if not the first boot.
  if (bootCount != 0) {
    struct tm timeinfo;
    char buf[40];
    gmtime_r(&nominalWakeTime.tv_sec, &timeinfo);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    while (!Serial) {
      delay(100);
    }
    Serial.println("time,temperature_esp32");
    Serial.printf("%s.%06ldZ,%f\n", buf, nominalWakeTime.tv_usec, temperature);
    Serial.printf("Compensated sample lag: %.6f seconds\n", sensorReadLagMicros/1e6f + adjustSleepSeconds);
  }

  // On the first boot, also scan for available WiFi hotspots for debugging purposes
  if (bootCount == 0) {
    // Scan for available WiFi hotspots and check if the configured SSID is among them
    Serial.print("Scanning WiFi ...");
    int networkCount = WiFi.scanNetworks();
    Serial.println(" DONE");

    bool foundConfiguredSsid = false;
    for (int i = 0; i < networkCount; i++) {
      bool isConfigured = (strcmp(WiFi.SSID(i).c_str(), wifi_ssid) == 0);
      foundConfiguredSsid |= isConfigured;
      Serial.printf("%d: %s  (%" PRIi32 " dBm)  %s%s\n",
          i,
          WiFi.SSID(i).c_str(),
          WiFi.RSSI(i),
          (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED",
          (isConfigured) ? "  Matches the configured SSID" : ""
      );
    }

    if (!foundConfiguredSsid) {
      Serial.println("Warning: Configured WiFi SSID not found in scan.");
    }
  }

  // Connect to the configured WiFi hotspot
  Serial.printf("WiFi connecting to %s ...", wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);   // Helps with antenna design flaw in early ESP32-C3 Super Mini modules
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print(" DONE, got local ip ");
  Serial.println(WiFi.localIP());

  // Get ESP32 and DS1308 RTC time from Internet per NTP sync schedule
  // or if not scheduled for this boot, get ESP32 time from DS1308 RTC
  if (bootCount % ntpSyncIntervalSamplingPeriods == 0 || rtcYear < 2025) {
    // Sync ESP32 time from NTP
    Serial.print("Syncing time from NTP ...");
    time_t now = 0;
    struct tm timeinfo;
    configTzTime(time_zone, ntpServerPrimary, ntpServerSecondary);
    for(;;) {
        time(&now);
        gmtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2025 - 1900)) {
          break;
        }
        Serial.print(".");
        delay(500);
    }
    Serial.println(" DONE");
    Serial.println("Current time:");
    char timeStr[40];
    formatUtcTimeIso(now, timeStr, sizeof(timeStr));
    Serial.print("ESP32      ");
    Serial.println(timeStr);

    // Sync DS1308 RTC from ESP32 UTC time
    Serial.print("Syncing DS1308 RTC from ESP32 ...");
    syncRtcFromEsp32();
    Serial.println(" DONE");
  } else {
    // Print time until next NTP sync
    uint32_t remainder = bootCount % ntpSyncIntervalSamplingPeriods;
    Serial.printf("Boots remaining until NTP sync: %" PRIu32 "\n", (remainder == 0) ? 0 : ntpSyncIntervalSamplingPeriods - remainder);

    // Sync ESP32 UTC time from DS1308 RTC
    Serial.print("Syncing ESP32 time from DS1308 RTC ...");
    syncEsp32FromRtc();
    Serial.println(" DONE");
  }

  // Print time
  Serial.println("Current time:");
  char esp32TimeStr[40];
  char rtcTimeStr[40];
  getEsp32UtcTimeString(esp32TimeStr, sizeof(esp32TimeStr));
  getRtcUtcTimeString(rtcTimeStr, sizeof(rtcTimeStr));
  Serial.print("ESP32      ");
  Serial.println(esp32TimeStr);
  Serial.print("DS1308 RTC ");
  Serial.println(rtcTimeStr);

  // Calculate deep sleep duration to wake at next sampling time
  struct timeval currentTime;
  gettimeofday(&currentTime, NULL);
  int64_t sleepMicros = microsecondsUntilNextSample(currentTime, samplingPeriodMicros);
  uint64_t totalMicros = (uint64_t)currentTime.tv_sec * MICROS_PER_SECOND + currentTime.tv_usec + sleepMicros;
  nominalWakeTime.tv_sec = totalMicros / MICROS_PER_SECOND;
  nominalWakeTime.tv_usec = totalMicros % MICROS_PER_SECOND;
  char wakeTimeStr[40];
  formatUtcTimeIsoMicros(nominalWakeTime, wakeTimeStr, sizeof(wakeTimeStr));
  Serial.print("Will sleep until ");
  Serial.println(wakeTimeStr);
  sleepMicros += adjustSleepSeconds*1e6f;
  if (sleepMicros < 0) {
    sleepMicros = 0;
  }

  // Go to deep sleep
  bootCount++; 
  Serial.flush();
  esp_sleep_enable_timer_wakeup(sleepMicros);
  esp_deep_sleep_start();
}

void loop() {
  // Not used because we enter deep sleep in setup()
}