#pragma once
#include <Arduino.h>

// Final working pin plan for the NorthStar telemetry ESP32-S3.

// Native TWAI -> SN65HVD230
constexpr gpio_num_t PIN_CAN_TX = GPIO_NUM_17;
constexpr gpio_num_t PIN_CAN_RX = GPIO_NUM_16;

// Nano UART. RX is the existing telemetry path; TX is the deliberately
// limited return-control path to Nano D0/RX through 220 Ω + removable jumper.
// 220 Ω is field-proven; 1 kΩ did not overcome the Nano USB-serial RX0 bias.
constexpr int PIN_NANO_RX = 18;
constexpr int PIN_NANO_TX = 8;

// microSD SPI
constexpr int PIN_SD_CS   = 10;
constexpr int PIN_SD_MOSI = 11;
constexpr int PIN_SD_SCK  = 12;
constexpr int PIN_SD_MISO = 13;
