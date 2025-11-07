#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>
#include <Wire.h>
#include <RTClib.h>

// Secrets
// -------

// In Secrets.h, configure:
// wifi_ssid and wifi_password
// time_zone
#include "Secrets.h"

// Helpful constants
// -----------------

// Conversion factor from microseconds to seconds
constexpr uint64_t MICROS_PER_SECOND = 1000000ULL;

// Configuration
// -------------

// Sampling interval in microseconds
constexpr uint64_t samplingPeriodMicros = MICROS_PER_SECOND * 30; // 30 seconds

// I2C Pins (DS1308 RTC)
constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;

// Serial monitor
constexpr uint32_t SERIAL_BAUD_RATE = 115200;

// NTP server
const char* ntpServerPrimary = "pool.ntp.org";
const char* ntpServerSecondary = "time.nist.gov";

// State
// -----

// DS1308 RTC (compatible with DS1307)
RTC_DS1307 rtc;

// Boot count in ESP32-C3 RTC memory, value retained over deep sleep
RTC_DATA_ATTR unsigned long bootCount = 0;

// Planned wake time (no plan initially, fill with zeros)
RTC_DATA_ATTR struct timeval wakeTime = {0, 0};

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

// Sync DS1308 RTC time from ESP32 at next clean second boundary
void syncRtcFromEsp32() {
  time_t t1, t2;

  // 1. Get initial time
  t1 = time(nullptr);

  // 2. Wait for the second to change
  for(;;) {
    t2 = time(nullptr);
    if (t2 != t1) {
      break;
    }
    delay(1);  // 1 ms polling. This doesn't need to be efficient.
  }

  // 3. t2 is the first moment of the new second → sync RTC now
  rtc.adjust(DateTime(t2));
}

// Sync ESP32 time from DS1308 RTC at a clean second boundary
void syncEsp32FromRtc() {
  DateTime t1, t2;

  // 1. Read initial RTC time
  t1 = rtc.now();

  // 2. Wait for the RTC second to change
  for(;;) {
    t2 = rtc.now();
    if (t2.second() != t1.second()) {
      break;
    }
    delay(1);  // 1 ms polling. We need to poll as RTC time can only be seen at 1s resolution
  }

  // 3. Sync ESP32 clock immediately after rollover
  struct timeval tv;
  tv.tv_sec  = t2.unixtime();
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

void printUtcTimeIsoMicros(const struct timeval& tv) {
  struct tm timeinfo;
  char buf[40];
  gmtime_r(&tv.tv_sec, &timeinfo);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  Serial.printf("%s.%06ldZ\n", buf, tv.tv_usec);
}

void printUtcTimeIso(const time_t& now) {
  struct tm timeinfo;
  char buf[30];
  gmtime_r(&now, &timeinfo);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  Serial.println(buf);
}

void printLocalTime(const time_t& now) {
  struct tm timeinfo;
  char buf[30];
  localtime_r(&now, &timeinfo);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S (local)", &timeinfo);
  Serial.println(buf);
}

void printEsp32UtcTime() {
  time_t now = time(nullptr);
  Serial.print("ESP32      ");
  printUtcTimeIso(now);
}

void printRtcUtcTime() {
  DateTime rtcDt = rtc.now();
  time_t rtcNow = rtcDt.unixtime();
  Serial.print("DS1308 RTC ");
  printUtcTimeIso(rtcNow);
}

// Arduino setup and loop functions
// --------------------------------

void setup() {
  // Setup serial for serial monitor
  Serial.begin(SERIAL_BAUD_RATE);

  // Print program name
  Serial.println("==============================================");
  Serial.printf("ESP32-C3 Data Logger (bootCount = %llu)\n", bootCount);
  Serial.println("==============================================");

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
  if (!rtc.begin()) {
    Serial.println(" FAILED!");
    Serial.println("ERROR: Couldn't find RTC. Check wiring!");
    while (1) delay(10);
  } else {
    while (!rtc.isrunning()) {
      Serial.print(".");
      delay(1000);
    }
  }
  Serial.println(" DONE");

  DateTime rtcDt = rtc.now();
  time_t rtcNow = rtcDt.unixtime();
  Serial.print("DS1308 RTC ");
  printUtcTimeIso(rtcNow);

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
      Serial.printf("%d: %s  (%d dBm)  %s%s\n",
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
  Serial.printf("Connecting to %s ...", wifi_ssid);
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
  if (bootCount == 0) {
    Serial.println("First boot, let's get the time from Internet!");

    // Get current time from NTP server and set time zone
    Serial.print("Waiting for time sync ...");
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

    Serial.println("Time after sync from NTP server:");
    Serial.print("ESP32      ");
    printUtcTimeIso(now);

    // Sync DS1308 RTC with ESP32 UTC time
    Serial.print("Syncing DS1308 RTC from ESP32 ...");
    syncRtcFromEsp32();
    Serial.println(" DONE");

  } else {
    Serial.println("It's not the first boot, DS1308 RTC has been keeping time.");

    // Sync ESP32 UTC time with DS1308 RTC
    // This only has 1 second accuracy and might not be needed if only RTC time is used
    Serial.print("Syncing ESP32 time from DS1308 RTC ...");
    syncEsp32FromRtc();
    Serial.println(" DONE");

    // Print nominal sensing time
    Serial.print("Nominal timestamp ");
    printUtcTimeIsoMicros(wakeTime);
  }

  // Print 
  printEsp32UtcTime()
  printRtcUtcTime()

  // Get current time with microsecond resolution
  struct timeval currentTime;
  gettimeofday(&currentTime, NULL);

  // Calculate sleep duration to wake at next sampling time
  uint64_t sleepMicros = microsecondsUntilNextSample(currentTime, samplingPeriodMicros);

  Serial.println("\n--- Deep Sleep Configuration ---");
  Serial.print("Current UTC time: ");
  printUtcTimeIsoMicros(currentTime);
  Serial.printf("Sleep duration: %llu microseconds (%.6f seconds)\n",
                sleepMicros,
                sleepMicros / 1000000.0);

  // Calculate and print expected wake time
  uint64_t totalMicros =
      (uint64_t)currentTime.tv_sec * MICROS_PER_SECOND +
      currentTime.tv_usec +
      sleepMicros;

  wakeTime.tv_sec = totalMicros / MICROS_PER_SECOND;
  wakeTime.tv_usec = totalMicros % MICROS_PER_SECOND;

  Serial.print("Expected wake time (UTC): ");
  printUtcTimeIsoMicros(wakeTime);

  // Go to deep sleep
  bootCount++;               // Increment boot count before sleep
  Serial.flush();            // Flush serial monitor
  esp_sleep_enable_timer_wakeup(sleepMicros);
  esp_deep_sleep_start();
}

void loop() {
  // Not used because we enter deep sleep in setup()
}
