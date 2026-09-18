#pragma once
#include <Arduino.h>

constexpr uint32_t USB_SERIAL_BAUD = 115200;
constexpr uint32_t NANO_SERIAL_BAUD = 115200;
constexpr uint32_t CAN_BITRATE = 500000;

constexpr uint32_t MQTT_FAST_PERIOD_MS = 1000;
constexpr uint32_t MQTT_SLOW_PERIOD_MS = 10000;
constexpr uint32_t WIFI_RETRY_PERIOD_MS = 10000;

constexpr uint32_t RPM_CAN_ID = 0x0C665500UL;
