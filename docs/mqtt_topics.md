# MQTT Topics

Base topic:

`wayne/compressor`

Planned retained/telemetry topics:

- `availability`
- `state`
- `fault`
- `rpm`
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
- `can/rx_count`
- `can/error_count`
- `sd/status`

Raw CAN frames are not intended to be published continuously to Home Assistant.
