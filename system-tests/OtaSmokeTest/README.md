# OTA Smoke Test

This is a planned minimal system test for validating OTA upload mechanics on the Arduino Nano 33 IoT before OTA is integrated into production firmware or more complex diagnostic tests.

## Purpose

The OTA smoke test validates only the lowest-level OTA workflow:

* connect to WiFi
* report IP address and signal status
* start OTA service
* keep polling OTA regularly
* allow upload by fixed IP or normal DNS hostname
* reboot into the updated sketch
* remain OTA-capable after the update

It must not depend on MQTT, Home Assistant, sensors, actuators, persistent configuration, RTC, EEPROM, or production firmware modules.

## Non-Goals

This test must not validate:

* production firmware behavior
* Home Assistant integration
* MQTT discovery
* actuator control
* sensor correctness
* light scheduling
* fallback behavior
* persistent configuration

Those belong to later tests.

## Hardware Under Test

* Arduino Nano 33 IoT
* WiFiNINA network stack
* local WiFi network
* OTA upload path using the JAndrassy ArduinoOTA library

No external sensors or actuators are required.

## Network Assumptions

The board is expected to have a fixed IP address or a stable DNS hostname.

mDNS is not required for this project's default workflow. It may be enabled later as a compile-time option for users whose networks support it reliably.

The upload machine must be able to reach the board on the OTA upload port.

## Planned Upload Workflow

Initial flash:

1. Compile the smoke test.
2. Upload it once by USB.
3. Open Serial Monitor.
4. Verify WiFi connection, IP address, RSSI, and OTA status.
5. Close Serial Monitor before OTA upload.

OTA update:

1. Export or locate the compiled binary.
2. Upload with the `arduinoOTA` tool using the board IP address or DNS hostname.
3. Use the fixed username required by the OTA tool/library.
4. Use the configured OTA password.
5. Wait for upload completion and board reboot.
6. Reopen Serial Monitor or inspect network status.
7. Verify that the updated sketch still starts OTA.

## Expected Serial Output

The test should print at least:

```text
OTA Smoke Test
WiFi: connecting
WiFi: connected
IP: <address>
RSSI: <dBm>
OTA: enabled
Heartbeat: <counter>
```

The heartbeat should continue at a predictable interval while the test is idle.

If WiFi disconnects, the test should report the state and keep trying to reconnect.

## Expected Behavior

The test should:

* keep running without USB after the first flash
* accept OTA upload by fixed IP or DNS hostname
* not require mDNS
* not require MQTT
* not require Home Assistant
* not block OTA polling with long delays
* remain recoverable by USB if a later sketch removes OTA support

## Safety Notes

This test must not control external actuators.

It is safe to run with only USB power or the normal board power supply, assuming the board is wired safely and no actuator code is added.

## Output Capture

Serial output is sufficient for the first version.

A later version may also publish basic MQTT status to the system-test topic namespace, but MQTT must remain optional and must not be required for OTA.

## Open Questions

* Should the first implementation include MQTT status, or should it remain Serial-only?
* Should mDNS be disabled by default with a compile-time option to enable it?
* Where should shared credentials live once more OTA-capable tests exist?
* Should a small local upload helper script be added after the first manual OTA workflow is stable?
