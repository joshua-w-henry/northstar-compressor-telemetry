# Home Assistant dashboard

This view uses only built-in Home Assistant cards and the MQTT Discovery entity IDs published by the ESP32 firmware.

Paste the YAML below into a new dashboard/view using **Edit dashboard -> Add view -> YAML mode** (or create the cards manually with the same entities).

```yaml
title: Compressor
path: compressor
icon: mdi:air-compressor
type: sections
max_columns: 3
sections:
  - type: grid
    cards:
      - type: heading
        heading: NorthStar Compressor
        icon: mdi:air-compressor

      - type: tile
        entity: sensor.northstar_compressor_state
        name: State
        icon: mdi:state-machine

      - type: tile
        entity: binary_sensor.northstar_compressor_running
        name: Engine
        icon: mdi:engine

      - type: tile
        entity: sensor.northstar_compressor_fault
        name: Fault
        icon: mdi:alert-circle-outline

      - type: gauge
        entity: sensor.northstar_compressor_pressure_psi
        name: Tank Pressure
        min: 0
        max: 200
        needle: true

      - type: gauge
        entity: sensor.northstar_compressor_engine_rpm
        name: Engine RPM
        min: 0
        max: 4000
        needle: true

      - type: tile
        entity: sensor.northstar_compressor_battery_voltage
        name: Battery
        icon: mdi:car-battery

  - type: grid
    cards:
      - type: heading
        heading: Controller
        icon: mdi:chip

      - type: entities
        title: Controller State
        entities:
          - entity: sensor.northstar_compressor_mode
            name: Mode
          - entity: sensor.northstar_compressor_auto_switch
            name: AUTO / OFF
          - entity: sensor.northstar_compressor_pressure_switch
            name: Pressure Switch
          - entity: binary_sensor.northstar_compressor_master
            name: Master
          - entity: binary_sensor.northstar_compressor_start_stop
            name: Start / Stop
          - entity: binary_sensor.northstar_compressor_unloader
            name: Unloader
          - entity: binary_sensor.northstar_compressor_idle
            name: Idle
          - entity: binary_sensor.northstar_compressor_kill
            name: Kill

      - type: entities
        title: Runtime
        entities:
          - entity: sensor.northstar_compressor_hobbs_hours
            name: Hobbs
          - entity: sensor.northstar_compressor_start_cycles
            name: Start Cycles
          - entity: sensor.northstar_compressor_last_event
            name: Last Event

  - type: grid
    cards:
      - type: heading
        heading: Telemetry Health
        icon: mdi:heart-pulse

      - type: entities
        title: ESP32 / Logger
        entities:
          - entity: sensor.northstar_compressor_wifi_rssi
            name: Wi-Fi RSSI
          - entity: sensor.northstar_compressor_sd_status
            name: SD Status
          - entity: binary_sensor.northstar_compressor_sd_mounted
            name: SD Mounted
          - entity: sensor.northstar_compressor_sd_session
            name: SD Session
          - entity: sensor.northstar_compressor_nano_uart_lines
            name: Nano UART Lines
          - entity: sensor.northstar_compressor_can_rx_count
            name: CAN RX Frames
          - entity: sensor.northstar_compressor_can_error_count
            name: CAN Errors

      - type: history-graph
        title: Pressure / RPM / Battery
        hours_to_show: 2
        entities:
          - sensor.northstar_compressor_pressure_psi
          - sensor.northstar_compressor_engine_rpm
          - sensor.northstar_compressor_battery_voltage
```

## Expected MQTT Discovery entities

The firmware publishes retained Home Assistant discovery configuration under `homeassistant/...` whenever MQTT connects. The expected entities include:

- `sensor.northstar_compressor_state`
- `binary_sensor.northstar_compressor_running`
- `sensor.northstar_compressor_engine_rpm`
- `sensor.northstar_compressor_pressure_psi`
- `sensor.northstar_compressor_battery_voltage`
- `sensor.northstar_compressor_fault`
- `sensor.northstar_compressor_hobbs_hours`
- `sensor.northstar_compressor_start_cycles`
- `binary_sensor.northstar_compressor_master`
- `binary_sensor.northstar_compressor_start_stop`
- `binary_sensor.northstar_compressor_unloader`
- `binary_sensor.northstar_compressor_idle`
- `binary_sensor.northstar_compressor_kill`
- `sensor.northstar_compressor_wifi_rssi`
- `sensor.northstar_compressor_sd_status`
- `binary_sensor.northstar_compressor_sd_mounted`
- `sensor.northstar_compressor_sd_session`
- `sensor.northstar_compressor_nano_uart_lines`
- `sensor.northstar_compressor_can_rx_count`
- `sensor.northstar_compressor_can_error_count`

If Home Assistant chooses a slightly different generated entity ID, use the entity shown under **Settings -> Devices & services -> MQTT -> NorthStar Compressor** and substitute it in the dashboard YAML.
