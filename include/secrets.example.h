#pragma once

// Copy this file to include/secrets.h and fill in local credentials.
// secrets.h is ignored by Git.

#define WIFI_SSID_1 "Bald Bunny"
#define WIFI_PASS_1 "CHANGE_ME"

#define WIFI_SSID_2 "KOTH"
#define WIFI_PASS_2 "CHANGE_ME"

#define MQTT_HOST "192.168.0.1"
#define MQTT_PORT 1883
#define MQTT_USER "CHANGE_ME"
#define MQTT_PASS "CHANGE_ME"

#define MQTT_CLIENT_ID "northstar-compressor-telemetry"
#define MQTT_BASE_TOPIC "wayne/compressor"

// Set any non-empty custom password in ignored secrets.h (no minimum length).
// Missing/placeholder value disables OTA entirely.
#define OTA_PASSWORD "CHANGE_ME"
