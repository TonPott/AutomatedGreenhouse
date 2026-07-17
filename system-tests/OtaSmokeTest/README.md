# OTA Uptime Smoke Test

This system test validates WiFi continuity, OTA availability, and periodic MQTT uptime reporting on an Arduino Nano 33 IoT. ArduinoHA registers the uptime sensor under the dedicated Home Assistant device **Grow Controller Tests**.

The test remains independent of production firmware, sensors, actuators, RTC, EEPROM, and persistence.

## Runtime Hierarchy

The runtime order is fixed:

1. connect WiFi
2. start or restart OTA after WiFi is connected
3. configure ArduinoHA MQTT after OTA is available
4. poll OTA before and after network service calls
5. monitor WiFi continuously and reconnect indefinitely

MQTT or Home Assistant failure does not disable OTA. WiFi reconnect is implemented as a state machine and the sketch contains no `delay()` calls. WiFi and MQTT socket operations use one-second timeouts. The sketch records and reports any observed OTA poll gap longer than two seconds.

## Hardware Under Test

- Arduino Nano 33 IoT
- WiFiNINA network stack
- JAndrassy ArduinoOTA with `InternalStorage`
- MQTT broker with Home Assistant MQTT discovery enabled

No external sensors or actuators are required.

## Credentials

Copy `Credentials.example.h` to the ignored local file `Credentials.h`, then set:

- WiFi SSID and password
- MQTT host, port, username, and password
- Home Assistant MQTT discovery prefix
- OTA name and a strong OTA password

The OTA password is independent of the WiFi password. Never commit `Credentials.h`.

## MQTT and Home Assistant

ArduinoHA creates one numeric sensor:

- device: `Grow Controller Tests`
- device ID: `grow_controller_tests_ota`
- entity name: `Uptime`
- entity unique ID: `grow_controller_tests_ota_uptime_seconds`
- unit: seconds
- device class: `duration`
- state class: `total_increasing`

The ArduinoHA data prefix is:

```text
smaeenhouse/test/ota_uptime
```

Subscribe to `smaeenhouse/test/ota_uptime/#` to inspect the generated uptime state topic directly. The value is published immediately after each MQTT connection and every 30 seconds while connected. The entity expires after 90 seconds without a state update, and ArduinoHA republishes discovery and current state after reconnect.

## Compile and Initial Upload

Compile from the repository root:

```powershell
$env:SKETCH = "system-tests/OtaSmokeTest"
.\scripts\check-arduino.ps1
```

Upload the test once over USB. Serial Monitor is optional; the sketch never waits for it and runs normally without a connected PC.

## OTA Upload

After the first USB upload:

1. Obtain the board IP from Serial, DHCP leases, or local DNS.
2. Compile the next sketch version.
3. Upload with the `arduinoOTA` tool using the board IP or normal DNS hostname, `OTA_NAME`, and the password currently running on the board.
4. Verify that the board reboots, reconnects WiFi, restarts OTA, reconnects MQTT, and resets the published uptime.

mDNS is not required. A fixed IP or normal DNS hostname is the default workflow.

## Optional Serial Output

When a Serial host is connected at 115200 baud, the sketch reports:

- WiFi connect, loss, timeout, and reconnect events
- cumulative WiFi join, connect-timeout, and NINA module-reset counters
- assigned IP address and RSSI
- OTA readiness and any poll-gap violation over two seconds
- MQTT connection state
- every uptime publication
- a 30-second status summary

No Serial write is attempted while native USB Serial is unavailable.

## Expected Observations

- WiFi connects before OTA and MQTT start.
- OTA remains reachable while MQTT or Home Assistant is unavailable.
- A WiFi outage causes repeated non-blocking reconnect attempts until service returns.
- After three consecutive WiFi connect timeouts, the sketch reinitializes the NINA interface and continues retrying without rebooting the SAMD21.
- OTA is restarted after every WiFi reconnection.
- MQTT reconnects automatically through ArduinoHA.
- Uptime increases in 30-second publication steps and resets after an OTA reboot.
- `ota_gap_violations` remains zero during normal operation.

## Safety and Limitations

This test does not control actuators and is safe to run with the normal board supply or USB power when the installed wiring is otherwise safe.

The one-second MQTT socket timeout is intentionally defined before ArduinoHA is included so a failed broker interaction cannot consume the two-second OTA service budget. The `millis()`-based uptime rolls over after approximately 49.7 days; this is acceptable for the smoke test and is not a production uptime implementation.

WiFiNINA and MQTT library calls can still be influenced by firmware and network-stack behavior. The runtime poll-gap counter makes any observed violation visible instead of silently assuming that every library call met the budget.
