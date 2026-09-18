# Architecture

## Authority boundary

The Nano is the compressor controller. The ESP32-S3 is not part of the control loop.

The ESP32 may observe:

- Nano controller state over one-way UART
- raw engine CAN in TWAI listen-only mode
- its own power/network/storage health

The ESP32 may publish data externally, but there is no path for MQTT, Wi-Fi, USB, or Home Assistant to actuate compressor outputs.

## Data paths

```text
Engine CAN ----> SN65HVD230 ----> ESP32-S3 TWAI ----> SD raw log
                                           |
Nano TX ---> divider ---> ESP32 UART ------+----> SD event log
                                           |
                                           +----> MQTT ----> Home Assistant
                                           |
                                           +----> later idle-time upload ----> UNICORN
```

## Current logging behavior

The ESP32 creates a new numbered session pair on each boot:

- `/can_0001.csv`, `/can_0002.csv`, ...
- `/nano_0001.log`, `/nano_0002.log`, ...

The CAN file records every received classical CAN frame with:

- monotonic microsecond timestamp
- wall-clock epoch milliseconds when NTP is available
- sequence number
- standard/extended flag
- CAN identifier
- DLC
- up to eight data bytes

The Nano file preserves every complete UART line with monotonic and wall-clock timestamps.

Files are flushed once per second. Local SD logging is authoritative; Wi-Fi and MQTT are secondary and must not be required for capture.

## Time

The logger starts immediately without waiting for Wi-Fi. Monotonic timestamps are always present. Once Wi-Fi is available, NTP is requested and subsequent records also carry wall-clock epoch time.

## Next logging refinement

Once the Nano telemetry line format is frozen, session rotation can move from one-file-pair-per-boot to one-file-pair-per-compressor-run. Completed run logs can then be uploaded to UNICORN while idle.
