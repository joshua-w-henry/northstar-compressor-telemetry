# NorthStar Compressor Telemetry

ESP32-S3 sidecar for the NorthStar compressor controller.

## Architecture

The Nano remains the authoritative controller. The ESP32-S3 is primarily an
observer/logger/gateway, with one deliberately narrow permission control.

- Nano: control, safety, local OLED, FRAM counters, faults
- ESP32-S3: passive CAN capture, SD logging, Nano UART ingest, Wi-Fi/MQTT, remote AUTO permit gateway
- Home Assistant: live telemetry, alerts, maintenance reminders, history, and diagnostic IP address
- UNICORN: long-term raw CAN archive

The ESP32 must never be required for safe compressor operation.

## Hardware baseline

- ESP32-S3 N16R8 development board
- Waveshare SN65HVD230 3.3 V CAN transceiver
- microSD SPI module
- dedicated 5 V buck supply
- Nano TX -> divider -> ESP32 UART RX
- ESP32 GPIO 8 / TX -> 220 Ω -> removable jumper -> Nano D0 / RX
- existing compressor CAN H/L connection

The 220 Ω return-path resistor is field-proven. A 1 kΩ resistor was initially
tested but did not allow the ESP32 to pull Nano RX0 low enough because the Nano
onboard USB-serial interface also biases RX0. The measured RX node was about
4.2 V with 1 kΩ; changing to 220 Ω restored reliable UART control.

## Phase 1

1. Boot and USB serial diagnostics
2. Wi-Fi connection with Bald Bunny / KOTH fallback
3. MQTT connection and availability topic
4. TWAI listen-only CAN capture
5. SD raw CAN logging
6. Nano UART telemetry ingest
7. Home Assistant MQTT discovery
8. Manual read-only SD log pull over local HTTP
9. Limited Home Assistant remote AUTO permit ON/OFF

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

## Limited remote AUTO control

Home Assistant exposes a single `Remote Auto` switch. Its MQTT command topic is:

```text
wayne/compressor/remote_auto/set
```

Accepted payloads are `ON` and `OFF`. The ESP32 translates only those values
into the Nano serial commands `remote auto on` and `remote auto off`.

The Nano reports both the persisted remote permission and the effective AUTO
state. Effective AUTO requires both the physical selector and remote permission:

```text
effective AUTO = physical AUTO switch AND remote AUTO permit
```

Remote OFF is persisted by the Nano in FRAM. On firmware upgrade the new permit
defaults to ON so existing behavior is preserved. A deliberate physical
OFF -> AUTO cycle locally clears a remote inhibit, which preserves local recovery
if the ESP32, Wi-Fi, MQTT, or Home Assistant is unavailable.

## Safety boundary

The ESP32 cannot directly command engine start, Master, Start/Stop, Unloader,
Idle, Kill, or Reset through MQTT. CAN remains listen-only. The Nano remains the
authoritative controller and is not dependent on the ESP32 for safe operation.
