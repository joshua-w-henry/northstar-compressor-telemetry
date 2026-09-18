# NorthStar Compressor Telemetry

ESP32-S3 sidecar for the NorthStar compressor controller.

## Architecture

The Nano remains the authoritative controller. The ESP32-S3 is an observer/logger/gateway only.

- Nano: control, safety, local OLED, FRAM counters, faults
- ESP32-S3: passive CAN capture, SD logging, Nano UART ingest, Wi-Fi/MQTT
- Home Assistant: live telemetry, alerts, maintenance reminders, history
- UNICORN: long-term raw CAN archive

The ESP32 must never be required for safe compressor operation.

## Hardware baseline

- ESP32-S3 N16R8 development board
- Waveshare SN65HVD230 3.3 V CAN transceiver
- microSD SPI module
- dedicated 5 V buck supply
- one-way Nano TX -> divider -> ESP32 UART RX
- existing compressor CAN H/L connection

## Phase 1

1. Boot and USB serial diagnostics
2. Wi-Fi connection with Bald Bunny / KOTH fallback
3. MQTT connection and availability topic
4. TWAI listen-only CAN capture
5. SD raw CAN logging
6. Nano UART telemetry ingest
7. Home Assistant MQTT discovery
8. Idle-time log upload to UNICORN

## Safety boundary

The ESP32 does not issue compressor control commands. CAN starts in listen-only mode. Nano UART is receive-only in Phase 1.
