# Guarded ESP32-S3 over-the-air firmware updates

This is **ESP32 telemetry-sidecar firmware only**. The Arduino Nano remains
the independent, authoritative compressor controller.

## One-time setup (USB flash)

1. On ShopDell, open the repository in VS Code with PlatformIO.
2. Check out the `feature/guarded-esp32-ota` branch. If the branch is not
   visible locally, `git fetch origin` then
   `git switch --track origin/feature/guarded-esp32-ota`.
3. Ensure the ignored `include/secrets.h` contains working Wi-Fi and MQTT
   values. Add a unique 12+ character password:

   ```cpp
   #define OTA_PASSWORD "REPLACE_WITH_YOUR_OWN_LONG_RANDOM_PASSWORD"
   ```

   Do **not** use the placeholder literally, reuse any other service password,
   or commit `secrets.h`. Without a configured password, OTA is disabled.
4. Physically turn the compressor's **AUTO/OFF selector to OFF**, verify the
   engine is fully stopped, and leave the selector OFF throughout the upload.
5. First build: `pio run -e esp32-s3-devkitc-1`.
6. First deployment: use PlatformIO USB Upload with the
   `esp32-s3-devkitc-1` environment (or
   `pio run -e esp32-s3-devkitc-1 -t upload --upload-port COMx`).
   **An ESP32 without OTA firmware cannot be upgraded over Wi-Fi.**
7. Confirm the new ESP32 boots and reconnects to Wi-Fi, MQTT and Nano UART.
   Home Assistant should discover an `OTA Ready` binary sensor.

## Subsequent Wi-Fi uploads (ShopDell / PowerShell)

While the selector is **OFF**, engine stopped, and Nano UART status fresh:

```powershell
# Run from the repository folder. Read-Host avoids putting the password
# in PowerShell history. This setting lasts only for this terminal session.
$env:NORTHSTAR_OTA_PASSWORD = Read-Host "Compressor OTA password"
$env:PLATFORMIO_UPLOAD_FLAGS = "--auth=$env:NORTHSTAR_OTA_PASSWORD"

# Replace the IP with the actual ESP32 address shown in Home Assistant.
pio run -e esp32-s3-devkitc-1-ota -t upload --upload-port 192.168.86.X

# Clear credential-bearing environment variables after the transfer.
Remove-Item Env:NORTHSTAR_OTA_PASSWORD
Remove-Item Env:PLATFORMIO_UPLOAD_FLAGS
```

Or choose the PlatformIO `esp32-s3-devkitc-1-ota` environment for an upload
while both environment variables are set. The host and device password must
match. The OTA upload uses ESP32 port 3232 on the local network.

## Update interlocks

OTA **never runs without**:

- Wi-Fi and a local non-placeholder OTA password
- Valid Nano `STATUS` received within 2.5 seconds
- The Nano's physical selector reporting `sw=OFF`
- Nano reporting `run=N`, `rpm=0`, and both Master and Start/Stop outputs 0
- No recently reported **nonzero** engine RPM over CAN

The ESP32 only handles OTA invitations when all these checks pass; otherwise,
the network upload will time out. Loss of Nano serial telemetry fails closed.
The Home Assistant `OTA Ready` entity indicates the current interlock state;
this is not a substitute for visually verifying the physical selector.

**Important:** The ESP32 cannot mechanically lock the selector OFF. Do not
toggle AUTO or attempt to start the engine during a transfer. OTA writing can
temporarily block the ESP32 logger loop and may create a short gap in CAN/UART
logging. The Nano continues operating independently. OTA is not a control or
safety channel.

Do not port-forward ESP32 OTA or the log HTTP server. ArduinoOTA password
authentication is **not** signed-firmware verification or a full encrypted
update channel. Restrict access to trusted local network clients.

## If it does not work

- **Timeout:** check Nano `sw=OFF`, UART/status freshness, correct ESP32 IP,
  same reachable network, Windows firewall (Private network), and UDP 3232.
  The ESP32 must already have OTA-capable firmware from USB.
- **Authentication failed:** compare the locally compiled `OTA_PASSWORD`
  with `NORTHSTAR_OTA_PASSWORD` typed on ShopDell.
- **No OTA Ready entity:** verify new firmware boots, MQTT is connected,
  and the Nano status parser receives `STATUS` messages.
- **First USB upload fails:** confirm serial COM port and ESP32 USB drivers.
- **Recovery:** use USB to restore ESP32 firmware. Do not modify the Nano.

## Branch note

This was developed from `can-ecu-telemetry-ha` (four commits ahead of `main`
at the time work began). Do not accidentally replace it with older `main`
firmware. Before merging, build and validate on ShopDell and test the interlocks.
