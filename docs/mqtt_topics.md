# MQTT Topics

Base topic:

`wayne/compressor`

Current topics:

- `availability` (retained)
- `rpm`
- `can/status` (retained)
- `can/rx_count`
- `can/error_count`
- `can/logged_count`
- `can/log_drop_count`
- `nano/line_count`
- `sd/status` (retained)
- `sd/session` (retained)
- `wifi/rssi`

Planned decoded Nano/controller topics:

- `state`
- `fault`
- `pressure_psi`
- `battery_voltage`
- `master`
- `start_stop`
- `unloader`
- `idle`
- `kill`
- `cycles`
- `hobbs_hours`
- `crank_min_voltage`

Raw CAN frames are intentionally not published continuously to Home Assistant. The SD card is the authoritative raw capture path.
