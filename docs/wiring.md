# Wiring

## Power

```text
Compressor battery 12 V
  |
  +-- dedicated 5 V buck --> ESP32-S3 5V/VIN
                              |
                              +-- 3.3 V --> SN65HVD230 VCC
```

Use common control ground between Nano, ESP32, CAN transceiver, and CAN bus reference.

## CAN

```text
ESP32 GPIO17 --> SN65HVD230 TXD
ESP32 GPIO16 <-- SN65HVD230 RXD

SN65HVD230 CANH --> compressor CAN-H
SN65HVD230 CANL --> compressor CAN-L
```

Do not add another 120 ohm terminator to an already terminated CAN bus.

## Nano telemetry

Phase 1 is one-way only.

```text
Nano TX (5 V)
   |
  2.0 k
   |
   +----> ESP32 UART RX
   |
  3.3 k
   |
  GND
```

This yields about 3.1 V at the ESP32 RX input.

## SD

Initial SPI pin plan is in `include/pins.h`. Confirm the specific SD module voltage requirements before wiring.
