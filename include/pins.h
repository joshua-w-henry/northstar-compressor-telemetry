#pragma once
#include <Arduino.h>

// Initial ESP32-S3 pin plan.
// Verify against the exact carrier board before permanent wiring.

// Native TWAI -> SN65HVD230
constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_17;
constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_16;

// Nano one-way telemetry UART
constexpr int PIN_NANO_RX = 18;

// microSD SPI
// microSD SPI - diagnostic remap away from GPIO10-13
constexpr int PIN_SD_CS   = 4;
constexpr int PIN_SD_MOSI = 5;
constexpr int PIN_SD_SCK  = 6;
constexpr int PIN_SD_MISO = 7;
