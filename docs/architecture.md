# Architecture

## Authority boundary

The Nano is the compressor controller. The ESP32-S3 is not part of the control loop.

The ESP32 may observe:

- Nano controller state over one-way UART
- raw engine CAN in TWAI listen-only mode
- its own power/network/storage health

The ESP32 may publish data externally, but Phase 1 provides no path for MQTT, Wi-Fi, USB, or Home Assistant to actuate compressor outputs.

## Data paths

```text
Engine CAN ----> SN65HVD230 ----> ESP32-S3 TWAI ----> SD raw log
                                           |
Nano TX ---> divider ---> ESP32 UART ------+----> MQTT ----> Home Assistant
                                           |
                                           +----> idle-time upload ----> UNICORN
```

## Logging philosophy

Raw CAN is retained for later analysis. Home Assistant receives decoded and operationally useful signals rather than the entire raw CAN firehose.

Each compressor run should eventually become a separate log file with timestamped raw CAN frames plus controller events.
