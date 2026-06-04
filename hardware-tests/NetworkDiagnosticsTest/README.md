# NetworkDiagnosticsTest

Standalone Arduino Nano 33 IoT network diagnostics sketch.

This test intentionally does not include the production `Credentials.h` file. Before uploading to real hardware, edit the `TEST_*` constants at the top of `NetworkDiagnosticsTest.ino` with local WiFi, MQTT, device, and NTP values.

The sketch checks:

- WiFi connection details
- forced WiFi reconnect with retry, useful after uploading while an older sketch was running
- DNS resolution for MQTT and NTP hosts
- TCP connectivity to the MQTT broker
- detailed NTP UDP request/response behavior across configurable servers
- standard and legacy NTP request headers for comparing server behavior
- WiFiNINA module time via `WiFi.getTime()` as an independent NTP-side check
- ArduinoHA MQTT connection and simulated Home Assistant entities

Serial commands:

- `h` print help
- `w` rerun WiFi, DNS, and MQTT TCP diagnostics
- `n` rerun detailed NTP diagnostics, including `WiFi.getTime()`
- `m` reconnect MQTT
- `p` publish all simulated Home Assistant states
- `r` rotate synthetic sensor values and publish
- `a` toggle diagnostic availability

Compile check:

```powershell
$env:SKETCH = "hardware-tests/NetworkDiagnosticsTest"
.\scripts\check-arduino.ps1
```

The local `Credentials.example.h` in this folder is a placeholder for the repository compile script only. The sketch does not include it.

## Findings

- `pool.ntp.org` is the preferred default external NTP server for this project.
- After uploading a new sketch while an older network test was running, the first WiFi connection attempt may fail because the WiFiNINA module still appears to be in an old connection state. This test intentionally forces `WiFi.disconnect()`, waits briefly, and retries before reporting WiFi failure.
- `WiFi.getTime()` is useful as an independent NTP-side check because it asks the WiFiNINA module for its own synchronized Unix time. A zero result means the module has no usable time.
- Diagnostic Home Assistant entities must use diagnostic-specific device/entity identifiers. Reusing production-style unique IDs can collide with old or production MQTT discovery entries in the Home Assistant entity registry.
- Network infrastructure policy is outside this repository. The firmware expectation is simply that the configured NTP server, normally `pool.ntp.org`, is reachable from the device over UDP port `123`.
