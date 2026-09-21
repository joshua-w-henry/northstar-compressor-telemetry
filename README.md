# NorthStar Compressor Telemetry

ESP32-S3 sidecar for the NorthStar compressor controller.

## Architecture

The Nano remains the authoritative controller. The ESP32-S3 is an observer/logger/gateway only.

- Nano: control, safety, local OLED, FRAM counters, faults
- ESP32-S3: passive CAN capture, SD logging, Nano UART ingest, Wi-Fi/MQTT
- Home Assistant: live telemetry, alerts, maintenance reminders, history, and diagnostic IP address
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
8. Manual read-only SD log pull over local HTTP

## SD log file pull

When the ESP32-S3 is connected to Wi-Fi, it exposes a small **read-only**
HTTP server for manual retrieval of SD logs.

Open:

```text
http://<ESP32-IP>/
```

The index page lists the generated `can_####.csv` and `nano_####.log`
files and provides download links. The server exposes no upload, edit, or
delete functions.

If the ESP32 boots without a card installed, it checks again every 5 seconds.
Inserting the card later automatically mounts it and starts a new logging session;
a reboot is not required. If an active card is removed and a subsequent log write
fails, the firmware drops the stale file handles and returns to the same remount
loop so reinsertion can recover automatically.

The active log files are flushed immediately before a download begins, so the
download is consistent up to that instant. A file transfer temporarily occupies
the ESP32 telemetry loop, so large downloads are best done while the compressor
is idle. The Nano remains the authoritative controller and is unaffected by an
ESP32 file transfer.

## Safety boundary

The ESP32 does not issue compressor control commands. CAN starts in listen-only mode. Nano UART is receive-only in Phase 1.
