#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <driver/twai.h>
#include <SPI.h>
#include <SD.h>

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

bool twaiReady = false;
bool sdReady = false;
uint32_t canRxCount = 0;
uint32_t canErrorCount = 0;
uint16_t engineRpm = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastFastPublishMs = 0;
unsigned long lastSlowPublishMs = 0;

static void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  const char* ssids[] = {WIFI_SSID_1, WIFI_SSID_2};
  const char* passes[] = {WIFI_PASS_1, WIFI_PASS_2};

  for (uint8_t i = 0; i < 2 && WiFi.status() != WL_CONNECTED; ++i) {
    if (!strcmp(ssids[i], "CHANGE_ME")) continue;

    Serial.printf("WIFI try %s\n", ssids[i]);
    WiFi.begin(ssids[i], passes[i]);

    const unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 7000) {
      delay(50);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WIFI OK %s IP=%s RSSI=%d\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  }
}

static void connectMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;

  String availability = String(MQTT_BASE_TOPIC) + "/availability";
  Serial.println("MQTT connect");

  if (mqtt.connect(MQTT_CLIENT_ID,
                   MQTT_USER,
                   MQTT_PASS,
                   availability.c_str(),
                   0,
                   true,
                   "offline")) {
    mqtt.publish(availability.c_str(), "online", true);
    Serial.println("MQTT OK");
  }
}

static bool startSd() {
  Serial.println("SD init");

  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

  // Start conservatively at 4 MHz for the first bench qualification.
  if (!SD.begin(PIN_SD_CS, SPI, 4000000)) {
    Serial.println("SD mount FAILED");
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("SD no card");
    return false;
  }

  Serial.printf("SD mounted size=%llu MB\n",
                (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));

  File file = SD.open("/bench.txt", FILE_APPEND);
  if (!file) {
    Serial.println("SD open /bench.txt FAILED");
    return false;
  }

  file.printf("BOOT ms=%lu NorthStar telemetry SD bench test\n",
              (unsigned long)millis());
  file.flush();
  file.close();
  Serial.println("SD write /bench.txt OK");

  file = SD.open("/bench.txt", FILE_READ);
  if (!file) {
    Serial.println("SD reopen /bench.txt FAILED");
    return false;
  }

  Serial.println("SD readback BEGIN");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("SD readback END");

  return true;
}

static bool startTwai() {
  twai_general_config_t general =
      TWAI_GENERAL_CONFIG_DEFAULT(PIN_CAN_TX, PIN_CAN_RX, TWAI_MODE_LISTEN_ONLY);

  // Compressor bus is currently known to run at 500 kbit/s.
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

    uint32_t id = msg.identifier;
    if (msg.extd && id == RPM_CAN_ID && msg.data_length_code >= 4) {
      engineRpm = ((uint16_t)msg.data[2] << 8) | msg.data[3];
    }

    // Phase 1 CAN vacuum: print frames to USB serial.
    // SD logging will consume the same frame stream next.
    Serial.printf("CAN %c %08lX [%u]",
                  msg.extd ? 'X' : 'S',
                  (unsigned long)id,
                  msg.data_length_code);
    for (uint8_t i = 0; i < msg.data_length_code; ++i) {
      Serial.printf(" %02X", msg.data[i]);
    }
    Serial.println();
  }

  twai_status_info_t status;
  if (twai_get_status_info(&status) == ESP_OK) {
    canErrorCount = status.rx_missed_count +
                    status.rx_overrun_count +
                    status.bus_error_count;
  }
}

static void processNanoUart() {
  // Phase 1: one-way Nano -> ESP32.
  // For now echo complete lines to USB serial. A structured parser comes next.
  static char line[160];
  static size_t len = 0;

  while (NanoSerial.available()) {
    char c = (char)NanoSerial.read();
    if (c == '\r') continue;

    if (c == '\n') {
      line[len] = 0;
      if (len) Serial.printf("NANO %s\n", line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    } else {
      len = 0;
    }
  }
}

static void publishFast() {
  if (!mqtt.connected()) return;

  char payload[24];
  snprintf(payload, sizeof(payload), "%u", engineRpm);
  String topic = String(MQTT_BASE_TOPIC) + "/rpm";
  mqtt.publish(topic.c_str(), payload, false);
}

static void publishSlow() {
  if (!mqtt.connected()) return;

  char payload[32];

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)canRxCount);
  String rxTopic = String(MQTT_BASE_TOPIC) + "/can/rx_count";
  mqtt.publish(rxTopic.c_str(), payload, false);

  snprintf(payload, sizeof(payload), "%lu", (unsigned long)canErrorCount);
  String errTopic = String(MQTT_BASE_TOPIC) + "/can/error_count";
  mqtt.publish(errTopic.c_str(), payload, false);
}

void setup() {
  Serial.begin(USB_SERIAL_BAUD);
  delay(300);
  Serial.println();
  Serial.println("NorthStar ESP32-S3 telemetry boot");

  WiFi.mode(WIFI_STA);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);

  NanoSerial.begin(NANO_SERIAL_BAUD, SERIAL_8N1, PIN_NANO_RX, -1);

  twaiReady = startTwai();
  sdReady = startSd();
  connectWifi();
  connectMqtt();
}

void loop() {
  processCan();
  processNanoUart();

  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED &&
      now - lastWifiAttemptMs >= WIFI_RETRY_PERIOD_MS) {
    lastWifiAttemptMs = now;
    connectWifi();
  }

  if (!mqtt.connected() && now - lastMqttAttemptMs >= 5000) {
    lastMqttAttemptMs = now;
    connectMqtt();
  }

  if (mqtt.connected()) mqtt.loop();

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
