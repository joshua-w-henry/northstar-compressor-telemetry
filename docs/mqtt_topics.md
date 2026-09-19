# MQTT Topics

Base topic:

`wayne/compressor`

## Human-readable compressor telemetry

The ESP32 parses the Nano controller's once-per-second `STATUS ...` line and publishes:

- `mode` — `AUTO` or `MAN`
- `state` — controller state such as `WAIT`, `RUN`, `OEM`, etc.
- `auto_switch` — physical AUTO/OFF switch state
- `running` — `Y` or `N`
- `rpm` — engine RPM
- `pressure_psi` — tank pressure
- `battery_voltage` — controller battery voltage
- `pressure_switch` — `CALL` or `FULL`
- `master_monitor` — physical master rocker monitor
- `master` — commanded Master output, 0/1
- `start_stop` — commanded Start/Stop output, 0/1
- `unloader` — commanded unloader output, 0/1
- `idle` — commanded idle output, 0/1
- `kill` — commanded kill output, 0/1
- `start_pulse_ms` — configured Start/Stop pulse duration
- `fault` — current controller fault
- `hobbs_hours` — accumulated runtime hours
- `cycles` — accumulated start cycles
- `event` — Nano `EV ...` messages such as crank/start/fault events
- `controller_message` — other high-level Nano messages such as `AUTO` or `MANUAL`

## Logger / health telemetry

- `availability` (retained)
- `can/status` (retained)
- `can/rx_count`
- `can/error_count`
- `can/logged_count`
- `can/log_drop_count`
- `nano/line_count`
- `nano/status_parse_ok`
- `nano/status_parse_error`
- `sd/status` (retained)
- `sd/session` (retained)
- `wifi/rssi`

The Nano UART line counter is independent of SD-card health. A failed SD mount therefore does not prevent UART reception or MQTT telemetry.

Raw CAN frames are intentionally not published continuously to Home Assistant. The SD card remains the authoritative raw CAN capture path.
