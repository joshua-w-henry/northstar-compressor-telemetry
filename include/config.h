#pragma once
#include <Arduino.h>

constexpr uint32_t USB_SERIAL_BAUD = 115200;
constexpr uint32_t NANO_SERIAL_BAUD = 115200;
constexpr uint32_t CAN_BITRATE = 500000;

constexpr uint32_t MQTT_FAST_PERIOD_MS = 1000;
constexpr uint32_t MQTT_SLOW_PERIOD_MS = 10000;
constexpr uint32_t MQTT_RETRY_PERIOD_MS = 5000;

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t WIFI_RETRY_PERIOD_MS = 2000;

constexpr uint32_t SD_SPI_HZ = 1000000;
constexpr uint8_t SD_INIT_ATTEMPTS = 3;
constexpr uint32_t SD_INIT_RETRY_DELAY_MS = 300;
constexpr uint32_t SD_FLUSH_PERIOD_MS = 1000;
constexpr uint32_t SD_HEALTH_PERIOD_MS = 30000;

constexpr uint32_t RPM_CAN_ID = 0x0C665500UL;
