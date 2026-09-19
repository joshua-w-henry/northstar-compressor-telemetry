#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <driver/twai.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <sys/time.h>

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
    return false;
  }

  snprintf(canPath, sizeof(canPath), "/can_%04u.csv", sessionNumber);
  snprintf(nanoPath, sizeof(nanoPath), "/nano_%04u.log", sessionNumber);

  canLog = SD.open(canPath, FILE_WRITE);
  nanoLog = SD.open(nanoPath, FILE_WRITE);

  if (!canLog || !nanoLog) {
    Serial.println("SD session log open FAILED");
    if (canLog) canLog.close();
    if (nanoLog) nanoLog.close();
    return false;
  }

  canLog.println("mono_us,epoch_ms,seq,frame,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7");
  nanoLog.println("# mono_ms,epoch_ms,line");
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
    Serial.printf("SD mount attempt %u/%u\n", attempt, SD_INIT_ATTEMPTS);

    if (SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ)) {
      if (SD.cardType() != CARD_NONE) {
        mounted = true;
        Serial.printf("SD mount OK on attempt %u\n", attempt);
        break;
      }

      Serial.println("SD card type NONE");
    } else {
      Serial.println("SD.begin failed");
    }

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
  const uint32_t monoUs = micros();

  canLog.printf("%lu,%llu,%lu,%c,%08lX,%u",
                (unsigned long)monoUs,
                (unsigned long long)epoch,
                (unsigned long)canLogCount,
                msg.extd ? 'X' : 'S',
                (unsigned long)msg.identifier,
                msg.data_length_code);

  for (uint8_t i = 0; i < 8; ++i) {
    if (i < msg.data_length_code) {
      canLog.printf(",%02X", msg.data[i]);
    } else {
      canLog.print(",");
    }
  }
  canLog.println();
  ++canLogCount;
}

static void logNanoLine(const char* line) {
  if (!sdReady || !nanoLog) return;

  nanoLog.printf("%lu,%llu,%s\n",
                 (unsigned long)millis(),
                 (unsigned long long)epochMsNow(),
                 line);
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
    publishText("sd/status", sdReady ? "online" : "fault", true);
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

  snprintf(payload, sizeof(payload), "%u", sessionNumber);
  publishText("sd/session", payload, true);
}

void setup() {
  Serial.begin(USB_SERIAL_BAUD);
  delay(300);
  Serial.println();
  Serial.println("NorthStar ESP32-S3 telemetry boot");

  WiFi.mode(WIFI_STA);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
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
