#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <driver/twai.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <sys/time.h>
#include <esp_timer.h>

#include "config.h"
#include "pins.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID_1 "CHANGE_ME"
#define WIFI_PASS_1 "CHANGE_ME"
#define WIFI_SSID_2 "CHANGE_ME"
#define WIFI_PASS_2 "CHANGE_ME"
#define MQTT_HOST "192.168.0.1"
#define MQTT_PORT 1883
#define MQTT_USER "CHANGE_ME"
#define MQTT_PASS "CHANGE_ME"
#define MQTT_CLIENT_ID "northstar-compressor-telemetry"
#define MQTT_BASE_TOPIC "wayne/compressor"
#endif

HardwareSerial NanoSerial(1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WebServer sdServer(SD_HTTP_PORT);
bool sdServerRoutesConfigured = false;
bool sdServerStarted = false;

File canLog;
File nanoLog;

bool twaiReady = false;
bool sdReady = false;
bool wifiAttemptActive = false;
bool wifiWasConnected = false;
bool ntpStarted = false;

uint8_t wifiIndex = 0;
uint16_t sessionNumber = 0;

uint32_t canRxCount = 0;
uint32_t canErrorCount = 0;
uint32_t canLogCount = 0;
uint32_t canLogDropCount = 0;
uint32_t nanoLineCount = 0;
uint16_t engineRpm = 0;

uint32_t sdMountAttempts = 0;
uint32_t sdMountFailures = 0;
uint32_t sdWriteErrors = 0;
uint64_t sdBytesWritten = 0;
unsigned long sdLastWriteMs = 0;
bool sdLastWriteOk = false;
char sdLastError[48] = "not_initialized";

struct ControllerStatus {
  bool valid = false;
  char mode[8] = "";
  char state[16] = "";
  char autoSwitch[8] = "";
  char running[4] = "";
  uint16_t rpm = 0;
  float pressurePsi = 0.0f;
  float batteryVoltage = 0.0f;
  char pressureSwitch[8] = "";
  char masterMonitor[8] = "";
  uint8_t master = 0;
  uint8_t startStop = 0;
  uint8_t unloader = 0;
  uint8_t idle = 0;
  uint8_t kill = 0;
  uint16_t startPulseMs = 0;
  char fault[20] = "";
  float hobbsHours = 0.0f;
  uint32_t cycles = 0;
};

ControllerStatus controllerStatus;
uint32_t nanoStatusParseOk = 0;
uint32_t nanoStatusParseError = 0;

unsigned long wifiAttemptStartedMs = 0;
unsigned long wifiNextAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastFastPublishMs = 0;
unsigned long lastSlowPublishMs = 0;
unsigned long lastSdFlushMs = 0;
unsigned long lastSdHealthPublishMs = 0;

static uint64_t epochMsNow() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1700000000) return 0;  // clock not synchronized yet
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static void publishText(const char* suffix, const char* value, bool retained = false) {
  if (!mqtt.connected()) return;
  String topic = String(MQTT_BASE_TOPIC) + "/" + suffix;
  mqtt.publish(topic.c_str(), value, retained);
}

static void setSdError(const char* error) {
  strncpy(sdLastError, error, sizeof(sdLastError) - 1);
  sdLastError[sizeof(sdLastError) - 1] = 0;
  sdLastWriteOk = false;
}

static const char* sdCardTypeName() {
  if (!sdReady) return "unavailable";

  switch (SD.cardType()) {
    case CARD_MMC:  return "MMC";
    case CARD_SD:   return "SD";
    case CARD_SDHC: return "SDHC_SD_XC";
    case CARD_NONE: return "none";
    default:        return "unknown";
  }
}

static void noteSdWrite(size_t expected, size_t actual, const char* context) {
  if (actual == expected) {
    sdLastWriteOk = true;
    sdLastWriteMs = millis();
    sdBytesWritten += actual;
    strncpy(sdLastError, "none", sizeof(sdLastError) - 1);
    sdLastError[sizeof(sdLastError) - 1] = 0;
    return;
  }

  ++sdWriteErrors;
  sdLastWriteOk = false;
  snprintf(sdLastError, sizeof(sdLastError), "%s_short_write", context);
}

static void publishSdHealth() {
  if (!mqtt.connected()) return;

  char payload[48];

  publishText("sd/status",
              !sdReady ? "mount_failed" :
              (sdLastWriteOk ? "online" : "degraded"),
              true);

  publishText("sd/mounted", sdReady ? "true" : "false", true);
  publishText("sd/card_type", sdCardTypeName(), true);
  publishText("sd/write_ok", sdLastWriteOk ? "true" : "false", false);
  publishText("sd/last_error", sdLastError, true);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)SD_SPI_HZ);
  publishText("sd/spi_hz", payload, true);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)sdMountAttempts);
  publishText("sd/mount_attempts", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)sdMountFailures);
  publishText("sd/mount_failures", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)sdWriteErrors);
  publishText("sd/write_errors", payload, false);

  snprintf(payload, sizeof(payload), "%llu", (unsigned long long)sdBytesWritten);
  publishText("sd/bytes_written", payload, false);

  snprintf(payload, sizeof(payload), "%u", sessionNumber);
  publishText("sd/session", payload, true);

  if (sdReady) {
    const uint64_t capacity = SD.cardSize();
    const uint64_t total = SD.totalBytes();
    const uint64_t used = SD.usedBytes();
    const uint64_t freeBytes = total >= used ? total - used : 0;

    snprintf(payload, sizeof(payload), "%llu",
             (unsigned long long)(capacity / (1024ULL * 1024ULL)));
    publishText("sd/capacity_mb", payload, true);

    snprintf(payload, sizeof(payload), "%llu",
             (unsigned long long)(used / (1024ULL * 1024ULL)));
    publishText("sd/used_mb", payload, false);

    snprintf(payload, sizeof(payload), "%llu",
             (unsigned long long)(freeBytes / (1024ULL * 1024ULL)));
    publishText("sd/free_mb", payload, false);

    const uint32_t freePercent = total ? (uint32_t)((freeBytes * 100ULL) / total) : 0;
    snprintf(payload, sizeof(payload), "%lu", (unsigned long)freePercent);
    publishText("sd/free_percent", payload, false);

    const unsigned long ageSec =
        sdLastWriteMs ? (millis() - sdLastWriteMs) / 1000UL : 0;
    snprintf(payload, sizeof(payload), "%lu", ageSec);
    publishText("sd/last_write_age_s", payload, false);
  }
}

static bool openSessionLogs() {
  if (!sdReady) return false;

  char canPath[24];
  char nanoPath[24];

  for (uint16_t n = 1; n < 10000; ++n) {
    snprintf(canPath, sizeof(canPath), "/can_%04u.csv", n);
    if (!SD.exists(canPath)) {
      sessionNumber = n;
      break;
    }
  }

  if (!sessionNumber) {
    Serial.println("SD no free session filename");
    setSdError("no_free_session_filename");
    return false;
  }

  snprintf(canPath, sizeof(canPath), "/can_%04u.csv", sessionNumber);
  snprintf(nanoPath, sizeof(nanoPath), "/nano_%04u.log", sessionNumber);

  canLog = SD.open(canPath, FILE_WRITE);
  nanoLog = SD.open(nanoPath, FILE_WRITE);

  if (!canLog || !nanoLog) {
    Serial.println("SD session log open FAILED");
    setSdError("session_open_failed");
    if (canLog) canLog.close();
    if (nanoLog) nanoLog.close();
    return false;
  }

  const char* canHeader = "mono_us,epoch_ms,seq,frame,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7\n";
  const char* nanoHeader = "# mono_ms,epoch_ms,line\n";
  noteSdWrite(strlen(canHeader),
              canLog.write((const uint8_t*)canHeader, strlen(canHeader)),
              "can_header");
  noteSdWrite(strlen(nanoHeader),
              nanoLog.write((const uint8_t*)nanoHeader, strlen(nanoHeader)),
              "nano_header");
  canLog.flush();
  nanoLog.flush();

  Serial.printf("SD session %04u logging ready\n", sessionNumber);
  return true;
}

static bool startSd() {
  Serial.printf("SD init @ %lu Hz\n", (unsigned long)SD_SPI_HZ);

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  delay(20);

  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  bool mounted = false;

  for (uint8_t attempt = 1; attempt <= SD_INIT_ATTEMPTS; ++attempt) {
    ++sdMountAttempts;
    Serial.printf("SD mount attempt %u/%u\n", attempt, SD_INIT_ATTEMPTS);

    if (SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ)) {
      if (SD.cardType() != CARD_NONE) {
        mounted = true;
        Serial.printf("SD mount OK on attempt %u\n", attempt);
        break;
      }

      Serial.println("SD card type NONE");
      setSdError("no_card");
    } else {
      Serial.println("SD.begin failed");
      setSdError("mount_failed");
    }

    ++sdMountFailures;
    SD.end();
    digitalWrite(PIN_SD_CS, HIGH);

    if (attempt < SD_INIT_ATTEMPTS) {
      delay(SD_INIT_RETRY_DELAY_MS);
    }
  }

  if (!mounted) {
    Serial.println("SD mount FAILED after retries");
    sdReady = false;
    return false;
  }

  Serial.printf("SD mounted size=%llu MB\n",
                (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));

  sdReady = true;
  sdLastWriteOk = true;
  strncpy(sdLastError, "none", sizeof(sdLastError) - 1);
  sdLastError[sizeof(sdLastError) - 1] = 0;
  if (!openSessionLogs()) {
    sdReady = false;
    return false;
  }

  return true;
}

static void flushLogsIfDue() {
  if (!sdReady) return;

  const unsigned long now = millis();
  if (now - lastSdFlushMs < SD_FLUSH_PERIOD_MS) return;

  lastSdFlushMs = now;
  if (canLog) canLog.flush();
  if (nanoLog) nanoLog.flush();
}

static void logCanFrame(const twai_message_t& msg) {
  if (!sdReady || !canLog) {
    ++canLogDropCount;
    return;
  }

  const uint64_t epoch = epochMsNow();
  const uint64_t monoUs = (uint64_t)esp_timer_get_time();

  char line[192];
  int len = snprintf(line, sizeof(line),
                     "%llu,%llu,%lu,%c,%08lX,%u",
                     (unsigned long long)monoUs,
                     (unsigned long long)epoch,
                     (unsigned long)canLogCount,
                     msg.extd ? 'X' : 'S',
                     (unsigned long)msg.identifier,
                     msg.data_length_code);

  if (len < 0 || len >= (int)sizeof(line)) {
    ++canLogDropCount;
    ++sdWriteErrors;
    setSdError("can_format_overflow");
    return;
  }

  for (uint8_t i = 0; i < 8 && len < (int)sizeof(line) - 5; ++i) {
    if (i < msg.data_length_code) {
      len += snprintf(line + len, sizeof(line) - len, ",%02X", msg.data[i]);
    } else {
      line[len++] = ',';
      line[len] = 0;
    }
  }

  if (len >= (int)sizeof(line) - 2) {
    ++canLogDropCount;
    ++sdWriteErrors;
    setSdError("can_format_overflow");
    return;
  }

  line[len++] = '\n';
  line[len] = 0;

  const size_t written = canLog.write((const uint8_t*)line, len);
  noteSdWrite((size_t)len, written, "can");

  if (written == (size_t)len) {
    ++canLogCount;
  } else {
    ++canLogDropCount;
  }
}

static void logNanoLine(const char* line) {
  if (!sdReady || !nanoLog) return;

  char record[256];
  const int len = snprintf(record, sizeof(record),
                           "%lu,%llu,%s\n",
                           (unsigned long)millis(),
                           (unsigned long long)epochMsNow(),
                           line);

  if (len <= 0 || len >= (int)sizeof(record)) {
    ++sdWriteErrors;
    setSdError("nano_format_overflow");
    return;
  }

  noteSdWrite((size_t)len,
              nanoLog.write((const uint8_t*)record, len),
              "nano");
}

static bool startTwai() {
  twai_general_config_t general =
      TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_RX, TWAI_MODE_LISTEN_ONLY);

  twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
    Serial.println("TWAI install failed");
    return false;
  }

  if (twai_start() != ESP_OK) {
    Serial.println("TWAI start failed");
    twai_driver_uninstall();
    return false;
  }

  Serial.println("TWAI listen-only 500k ready");
  return true;
}

static void processCan() {
  if (!twaiReady) return;

  twai_message_t msg;
  while (twai_receive(&msg, 0) == ESP_OK) {
    ++canRxCount;

    if (msg.extd && msg.identifier == RPM_CAN_ID && msg.data_length_code >= 4) {
      const uint16_t rpm = ((uint16_t)msg.data[2] << 8) | msg.data[3];
      if (rpm <= 4000) engineRpm = rpm;
    }

    logCanFrame(msg);
  }

  twai_status_info_t status;
  if (twai_get_status_info(&status) == ESP_OK) {
    canErrorCount = status.rx_missed_count +
                    status.rx_overrun_count +
                    status.bus_error_count;
  }
}

static void publishControllerState() {
  if (!mqtt.connected() || !controllerStatus.valid) return;

  char payload[40];

  publishText("mode", controllerStatus.mode, false);
  publishText("state", controllerStatus.state, false);
  publishText("auto_switch", controllerStatus.autoSwitch, false);
  publishText("running", controllerStatus.running, false);

  snprintf(payload, sizeof(payload), "%u", controllerStatus.rpm);
  publishText("rpm", payload, false);

  snprintf(payload, sizeof(payload), "%.1f", controllerStatus.pressurePsi);
  publishText("pressure_psi", payload, false);

  snprintf(payload, sizeof(payload), "%.2f", controllerStatus.batteryVoltage);
  publishText("battery_voltage", payload, false);

  publishText("pressure_switch", controllerStatus.pressureSwitch, false);
  publishText("master_monitor", controllerStatus.masterMonitor, false);

  snprintf(payload, sizeof(payload), "%u", controllerStatus.master);
  publishText("master", payload, false);

  snprintf(payload, sizeof(payload), "%u", controllerStatus.startStop);
  publishText("start_stop", payload, false);

  snprintf(payload, sizeof(payload), "%u", controllerStatus.unloader);
  publishText("unloader", payload, false);

  snprintf(payload, sizeof(payload), "%u", controllerStatus.idle);
  publishText("idle", payload, false);

  snprintf(payload, sizeof(payload), "%u", controllerStatus.kill);
  publishText("kill", payload, false);

  snprintf(payload, sizeof(payload), "%u", controllerStatus.startPulseMs);
  publishText("start_pulse_ms", payload, false);

  publishText("fault", controllerStatus.fault, false);

  snprintf(payload, sizeof(payload), "%.2f", controllerStatus.hobbsHours);
  publishText("hobbs_hours", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)controllerStatus.cycles);
  publishText("cycles", payload, false);
}

static bool parseControllerStatus(const char* line) {
  if (strncmp(line, "STATUS ", 7) != 0) return false;

  ControllerStatus parsed;

  const int matched = sscanf(
      line,
      "STATUS mode=%7s state=%15s sw=%7s run=%3s rpm=%hu psi=%f V=%f p=%7s mon=%7s | M=%hhu S=%hhu U=%hhu I=%hhu K=%hhu pulse=%hu fault=%19s hrs=%f cyc=%lu",
      parsed.mode,
      parsed.state,
      parsed.autoSwitch,
      parsed.running,
      &parsed.rpm,
      &parsed.pressurePsi,
      &parsed.batteryVoltage,
      parsed.pressureSwitch,
      parsed.masterMonitor,
      &parsed.master,
      &parsed.startStop,
      &parsed.unloader,
      &parsed.idle,
      &parsed.kill,
      &parsed.startPulseMs,
      parsed.fault,
      &parsed.hobbsHours,
      &parsed.cycles);

  if (matched != 18) {
    ++nanoStatusParseError;
    return false;
  }

  parsed.valid = true;
  controllerStatus = parsed;
  ++nanoStatusParseOk;
  publishControllerState();
  return true;
}

static void processNanoLine(const char* line) {
  ++nanoLineCount;

  if (parseControllerStatus(line)) return;

  if (strncmp(line, "EV ", 3) == 0) {
    publishText("event", line + 3, false);
  } else if (!strcmp(line, "MANUAL") || !strcmp(line, "AUTO")) {
    publishText("controller_message", line, false);
  }
}

static void processNanoUart() {
  static char line[192];
  static size_t len = 0;

  while (NanoSerial.available()) {
    const char c = (char)NanoSerial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      line[len] = 0;
      if (len) {
        Serial.printf("NANO %s\n", line);
        processNanoLine(line);
        logNanoLine(line);
      }
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    } else {
      len = 0;
    }
  }
}


static String normalizeSdPath(String path) {
  path.trim();
  if (!path.startsWith("/")) path = "/" + path;
  return path;
}

static bool isDownloadableLogPath(const String& path) {
  if (!path.startsWith("/")) return false;
  if (path.indexOf("..") >= 0) return false;
  if (path.indexOf('/', 1) >= 0) return false;

  return (path.startsWith("/can_") && path.endsWith(".csv")) ||
         (path.startsWith("/nano_") && path.endsWith(".log"));
}

static String urlEncode(const String& input) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(input.length() * 3);

  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t c = (uint8_t)input[i];

    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }

  return out;
}

static void flushLogsNow() {
  if (canLog) canLog.flush();
  if (nanoLog) nanoLog.flush();
}

static void handleSdIndex() {
  if (!sdReady) {
    sdServer.send(503, "text/plain",
                  "SD card is not available. Check Home Assistant SD diagnostics.\n");
    return;
  }

  String html;
  html.reserve(4096);
  html += F("<!doctype html><html><head><meta name=\"viewport\" "
            "content=\"width=device-width,initial-scale=1\">"
            "<title>NorthStar SD Logs</title></head><body>"
            "<h2>NorthStar Compressor SD Logs</h2>"
            "<p>Read-only file pull. No upload or delete functions are exposed.</p>"
            "<p><strong>Best practice:</strong> download while the compressor is idle. "
            "A large transfer temporarily occupies the ESP32 telemetry loop; "
            "the Nano controller is unaffected.</p><ul>");

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    sdServer.send(500, "text/plain", "Could not open SD root directory.\n");
    return;
  }

  bool found = false;
  File entry = root.openNextFile();

  while (entry) {
    if (!entry.isDirectory()) {
      String path = entry.name();
      if (!path.startsWith("/")) path = "/" + path;

      if (isDownloadableLogPath(path)) {
        found = true;
        char sizeText[32];
        snprintf(sizeText, sizeof(sizeText), "%llu",
                 (unsigned long long)entry.size());

        html += F("<li><a href=\"/download?file=");
        html += urlEncode(path);
        html += F("\">");
        html += path.substring(1);
        html += F("</a> &mdash; ");
        html += sizeText;
        html += F(" bytes</li>");
      }
    }

    entry.close();
    entry = root.openNextFile();
  }

  root.close();

  if (!found) {
    html += F("<li>No CAN/Nano log files found.</li>");
  }

  html += F("</ul><p><a href=\"/health\">SD server health</a></p></body></html>");
  sdServer.send(200, "text/html", html);
}

static void handleSdDownload() {
  if (!sdReady) {
    sdServer.send(503, "text/plain", "SD card is not available.\n");
    return;
  }

  if (!sdServer.hasArg("file")) {
    sdServer.send(400, "text/plain", "Missing file query parameter.\n");
    return;
  }

  const String path = normalizeSdPath(sdServer.arg("file"));
  if (!isDownloadableLogPath(path)) {
    sdServer.send(400, "text/plain", "Only root CAN CSV and Nano LOG files may be downloaded.\n");
    return;
  }

  // Make the active session consistent up to this instant before opening a
  // second read handle. loop() is blocked during streamFile(), so no new SD
  // writes occur until the transfer completes.
  flushLogsNow();

  File file = SD.open(path.c_str(), FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) file.close();
    sdServer.send(404, "text/plain", "File not found.\n");
    return;
  }

  String filename = path.substring(1);
  sdServer.sendHeader("Content-Disposition",
                      "attachment; filename=\"" + filename + "\"");
  sdServer.sendHeader("Cache-Control", "no-store");
  sdServer.sendHeader("Connection", "close");

  const char* contentType = path.endsWith(".csv")
                                ? "text/csv"
                                : "text/plain";

  Serial.printf("SD HTTP download start: %s (%llu bytes)\n",
                path.c_str(),
                (unsigned long long)file.size());

  sdServer.streamFile(file, contentType);
  file.close();

  Serial.printf("SD HTTP download complete: %s\n", path.c_str());
}

static void handleSdHealth() {
  char body[256];

  snprintf(body, sizeof(body),
           "northstar_sd_server=ok\n"
           "sd_mounted=%s\n"
           "session=%u\n"
           "ip=%s\n"
           "port=%u\n",
           sdReady ? "true" : "false",
           sessionNumber,
           WiFi.localIP().toString().c_str(),
           (unsigned)SD_HTTP_PORT);

  sdServer.send(sdReady ? 200 : 503, "text/plain", body);
}

static void startSdFileServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!sdServerRoutesConfigured) {
    sdServer.on("/", HTTP_GET, handleSdIndex);
    sdServer.on("/download", HTTP_GET, handleSdDownload);
    sdServer.on("/health", HTTP_GET, handleSdHealth);
    sdServer.onNotFound([]() {
      sdServer.send(404, "text/plain",
                    "Not found. Open / for the NorthStar SD log index.\n");
    });

    sdServerRoutesConfigured = true;
  }

  // Calling begin again after a Wi-Fi reconnect is harmless and ensures the
  // listener is attached to the current network interface.
  sdServer.begin();
  sdServerStarted = true;

  Serial.printf("SD HTTP server ready: http://%s:%u/\n",
                WiFi.localIP().toString().c_str(),
                (unsigned)SD_HTTP_PORT);
}

static void serviceSdFileServer() {
  if (!sdServerStarted || WiFi.status() != WL_CONNECTED) return;
  sdServer.handleClient();
}

static void beginWifiAttempt(uint8_t index) {
  const char* ssids[] = {WIFI_SSID_1, WIFI_SSID_2};
  const char* passes[] = {WIFI_PASS_1, WIFI_PASS_2};

  if (!strcmp(ssids[index], "CHANGE_ME") || !strlen(ssids[index])) {
    wifiIndex = (index + 1) % 2;
    wifiNextAttemptMs = millis() + WIFI_RETRY_PERIOD_MS;
    return;
  }

  WiFi.disconnect();
  Serial.printf("WIFI try %s\n", ssids[index]);
  WiFi.begin(ssids[index], passes[index]);

  wifiAttemptActive = true;
  wifiAttemptStartedMs = millis();
}

static void serviceWifi() {
  const wl_status_t status = WiFi.status();
  const unsigned long now = millis();

  if (status == WL_CONNECTED) {
    wifiAttemptActive = false;

    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.printf("WIFI OK %s IP=%s RSSI=%d\n",
                    WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());

      startSdFileServer();

      if (!ntpStarted) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        ntpStarted = true;
        Serial.println("NTP sync requested");
      }
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    sdServerStarted = false;
    Serial.println("WIFI lost");
  }

  if (wifiAttemptActive) {
    if (now - wifiAttemptStartedMs < WIFI_CONNECT_TIMEOUT_MS) return;

    wifiAttemptActive = false;
    WiFi.disconnect();
    wifiIndex = (wifiIndex + 1) % 2;
    wifiNextAttemptMs = now + WIFI_RETRY_PERIOD_MS;
    return;
  }

  if ((int32_t)(now - wifiNextAttemptMs) >= 0) {
    beginWifiAttempt(wifiIndex);
  }
}

static void publishHaDiscoveryEntity(const char* component,
                                     const char* objectId,
                                     const char* name,
                                     const char* stateSuffix,
                                     const char* extraJson = "") {
  if (!mqtt.connected()) return;

  char topic[160];
  char payload[900];

  snprintf(topic, sizeof(topic),
           "homeassistant/%s/northstar_compressor/%s/config",
           component, objectId);

  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\","
           "\"unique_id\":\"northstar_compressor_%s\","
           "\"object_id\":\"northstar_compressor_%s\","
           "\"state_topic\":\"%s/%s\","
           "\"availability_topic\":\"%s/availability\","
           "\"device\":{"
             "\"identifiers\":[\"northstar_compressor\"],"
             "\"name\":\"NorthStar Compressor\","
             "\"manufacturer\":\"Wayne\","
             "\"model\":\"Nano + ESP32-S3\""
           "}%s}",
           name,
           objectId,
           objectId,
           MQTT_BASE_TOPIC,
           stateSuffix,
           MQTT_BASE_TOPIC,
           extraJson);

  if (!mqtt.publish(topic, payload, true)) {
    Serial.printf("HA discovery publish failed: %s\n", objectId);
  }
}

static void publishHaDiscovery() {
  Serial.println("HA discovery publish");

  publishHaDiscoveryEntity("sensor", "state", "State", "state");
  publishHaDiscoveryEntity("sensor", "mode", "Mode", "mode");
  publishHaDiscoveryEntity("sensor", "auto_switch", "Auto Switch", "auto_switch");
  publishHaDiscoveryEntity("sensor", "pressure_switch", "Pressure Switch", "pressure_switch");
  publishHaDiscoveryEntity("sensor", "fault", "Fault", "fault");
  publishHaDiscoveryEntity("sensor", "event", "Last Event", "event");

  publishHaDiscoveryEntity(
      "sensor", "rpm", "Engine RPM", "rpm",
      ",\"unit_of_measurement\":\"rpm\",\"state_class\":\"measurement\"");

  publishHaDiscoveryEntity(
      "sensor", "pressure_psi", "Tank Pressure", "pressure_psi",
      ",\"device_class\":\"pressure\",\"unit_of_measurement\":\"psi\",\"state_class\":\"measurement\"");

  publishHaDiscoveryEntity(
      "sensor", "battery_voltage", "Battery Voltage", "battery_voltage",
      ",\"device_class\":\"voltage\",\"unit_of_measurement\":\"V\",\"state_class\":\"measurement\"");

  publishHaDiscoveryEntity(
      "sensor", "hobbs_hours", "Hobbs Hours", "hobbs_hours",
      ",\"unit_of_measurement\":\"h\",\"state_class\":\"total_increasing\"");

  publishHaDiscoveryEntity(
      "sensor", "cycles", "Start Cycles", "cycles",
      ",\"state_class\":\"total_increasing\"");

  publishHaDiscoveryEntity(
      "sensor", "wifi_rssi", "Wi-Fi RSSI", "wifi/rssi",
      ",\"device_class\":\"signal_strength\",\"unit_of_measurement\":\"dBm\",\"entity_category\":\"diagnostic\"");

  publishHaDiscoveryEntity(
      "sensor", "sd_status", "SD Status", "sd/status",
      ",\"entity_category\":\"diagnostic\"");
  publishHaDiscoveryEntity(
      "sensor", "sd_session", "SD Session", "sd/session",
      ",\"entity_category\":\"diagnostic\"");
  publishHaDiscoveryEntity(
      "sensor", "can_rx_count", "CAN RX Count", "can/rx_count",
      ",\"state_class\":\"total_increasing\",\"entity_category\":\"diagnostic\"");
  publishHaDiscoveryEntity(
      "sensor", "can_error_count", "CAN Error Count", "can/error_count",
      ",\"state_class\":\"total_increasing\",\"entity_category\":\"diagnostic\"");
  publishHaDiscoveryEntity(
      "sensor", "nano_line_count", "Nano UART Lines", "nano/line_count",
      ",\"state_class\":\"total_increasing\",\"entity_category\":\"diagnostic\"");

  publishHaDiscoveryEntity(
      "binary_sensor", "running", "Engine Running", "running",
      ",\"payload_on\":\"Y\",\"payload_off\":\"N\",\"device_class\":\"running\"");
  publishHaDiscoveryEntity(
      "binary_sensor", "master", "Master Output", "master",
      ",\"payload_on\":\"1\",\"payload_off\":\"0\"");
  publishHaDiscoveryEntity(
      "binary_sensor", "start_stop", "Start Stop Output", "start_stop",
      ",\"payload_on\":\"1\",\"payload_off\":\"0\"");
  publishHaDiscoveryEntity(
      "binary_sensor", "unloader", "Unloader", "unloader",
      ",\"payload_on\":\"1\",\"payload_off\":\"0\"");
  publishHaDiscoveryEntity(
      "binary_sensor", "idle", "Idle", "idle",
      ",\"payload_on\":\"1\",\"payload_off\":\"0\"");
  publishHaDiscoveryEntity(
      "binary_sensor", "kill", "Kill", "kill",
      ",\"payload_on\":\"1\",\"payload_off\":\"0\"");
  publishHaDiscoveryEntity(
      "binary_sensor", "sd_mounted", "SD Mounted", "sd/mounted",
      ",\"payload_on\":\"true\",\"payload_off\":\"false\",\"entity_category\":\"diagnostic\"");
}

static void connectMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;

  const String availability = String(MQTT_BASE_TOPIC) + "/availability";
  Serial.println("MQTT connect");

  if (mqtt.connect(MQTT_CLIENT_ID,
                   MQTT_USER,
                   MQTT_PASS,
                   availability.c_str(),
                   0,
                   true,
                   "offline")) {
    mqtt.publish(availability.c_str(), "online", true);
    publishHaDiscovery();
    publishSdHealth();
    publishText("can/status", twaiReady ? "listen_only" : "fault", true);
    publishControllerState();
    Serial.println("MQTT OK");
  }
}

static void serviceMqtt() {
  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    if (now - lastMqttAttemptMs >= MQTT_RETRY_PERIOD_MS) {
      lastMqttAttemptMs = now;
      connectMqtt();
    }
    return;
  }

  mqtt.loop();
}

static void publishFast() {
  if (!mqtt.connected()) return;

  char payload[24];
  const uint16_t rpm = controllerStatus.valid ? controllerStatus.rpm : engineRpm;
  snprintf(payload, sizeof(payload), "%u", rpm);
  publishText("rpm", payload, false);
}

static void publishSlow() {
  if (!mqtt.connected()) return;

  const unsigned long now = millis();
  char payload[40];

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)canRxCount);
  publishText("can/rx_count", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)canErrorCount);
  publishText("can/error_count", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)canLogCount);
  publishText("can/logged_count", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)canLogDropCount);
  publishText("can/log_drop_count", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)nanoLineCount);
  publishText("nano/line_count", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)nanoStatusParseOk);
  publishText("nano/status_parse_ok", payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)nanoStatusParseError);
  publishText("nano/status_parse_error", payload, false);

  snprintf(payload, sizeof(payload), "%d", WiFi.RSSI());
  publishText("wifi/rssi", payload, false);

  if (now - lastSdHealthPublishMs >= SD_HEALTH_PERIOD_MS) {
    lastSdHealthPublishMs = now;
    publishSdHealth();
  }
}

void setup() {
  Serial.begin(USB_SERIAL_BAUD);
  delay(300);
  Serial.println();
  Serial.println("NorthStar ESP32-S3 telemetry boot");

  WiFi.mode(WIFI_STA);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(1);
  mqtt.setKeepAlive(30);

  NanoSerial.begin(NANO_SERIAL_BAUD, SERIAL_8N1, PIN_NANO_RX, -1);

  twaiReady = startTwai();
  startSd();

  wifiNextAttemptMs = 0;
  serviceWifi();
}

void loop() {
  processCan();
  processNanoUart();
  flushLogsIfDue();

  serviceWifi();
  serviceSdFileServer();
  serviceMqtt();

  const unsigned long now = millis();

  if (now - lastFastPublishMs >= MQTT_FAST_PERIOD_MS) {
    lastFastPublishMs = now;
    publishFast();
  }

  if (now - lastSlowPublishMs >= MQTT_SLOW_PERIOD_MS) {
    lastSlowPublishMs = now;
    publishSlow();
  }

  delay(1);
}
