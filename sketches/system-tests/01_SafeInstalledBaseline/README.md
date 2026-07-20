# Safe Installed-System Baseline Test

This system test is the first sketch after the completed OTA smoke baseline. It proves that an OTA-capable sketch can run with the complete installed peripheral harness connected while all actuator outputs are held in known safe states.

The test intentionally does not publish Home Assistant discovery entities. It preserves the OTA smoke-test runtime hierarchy and publishes only direct MQTT test status under the separated system-test namespace.

## Purpose

- Verify that all connected actuators enter safe states immediately at boot.
- Keep safe states enforced during WiFi reconnect, MQTT outage, OTA polling, and normal idle operation.
- Confirm that installed interrupt-capable pins can be configured without doing I2C, MQTT, Home Assistant, or sensor work inside ISRs.
- Provide a safe baseline before any later sketch initializes I2C devices or actuates the fan/light hardware.

## Hardware Under Test

- Arduino Nano 33 IoT
- Installed output wiring for the fan switch, grow-light relay, and AD5263 `SHDN`
- Installed interrupt-capable inputs for SHT alert, DS3231 SQW/INT, fan tach, and the prepared light-sensor INT line
- WiFiNINA network stack
- JAndrassy ArduinoOTA with `InternalStorage`
- MQTT broker for direct test-topic publication

The test does not initialize sensors over I2C and does not intentionally read the soil sensor.

## Installed-System Assumptions

The full installed peripheral set may remain connected as documented in `HARDWARE.md` and the project schematic. The sketch assumes the production safe output polarity documented for the current build:

- fan switch off = `LOW` on pin `2`
- light relay open = `LOW` on pin `3`
- AD5263 shutdown asserted = `LOW` on pin `4`

If the real relay or switch stage behaves differently, stop the test and record that finding in the results section before creating the next sketch.

## Wiring Assumptions

| Signal | Pin | Test behavior |
|---|---:|---|
| SHT alert | `7` | `INPUT_PULLUP`, ISR sets a flag only |
| DS3231 SQW/INT | `10` | `INPUT_PULLUP`, ISR sets a flag only |
| Fan switch | `2` | Output held off |
| Fan tach | `A1` | `INPUT_PULLUP`, ISR counts pulses only |
| Light relay | `3` | Output held open |
| AD5263 `SHDN` | `4` | Output held asserted |
| Soil moisture | `A0` | Input only, no intentional reads |
| TSL25911 INT | `9` | `INPUT_PULLUP`, no ISR attached |

No I2C bus access is performed by this sketch.

## Credentials And Network Requirements

Copy `Credentials.example.h` to the ignored local file `Credentials.h`, then set:

- WiFi SSID and password
- MQTT host, port, username, and password
- OTA name and a strong OTA password

The OTA password is independent of the WiFi password. Never commit `Credentials.h`.

## Upload Procedure

Compile from the repository root:

```powershell
$env:SKETCH = "sketches/system-tests/01_SafeInstalledBaseline"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=sketches/system-tests/01_SafeInstalledBaseline ./scripts/check-arduino.sh
```

Upload the test once over USB or upload it over OTA from the completed `OtaSmokeTest` if that baseline is already running and reachable.

## Interaction Method

The sketch has no command interface. It establishes the safe output states once during `setup()` before network services start, does not provide any entity or command that can change those outputs, and publishes status periodically when MQTT is connected.

Serial Monitor at 115200 baud is optional. The sketch never waits for Serial and runs normally without a connected PC.

## MQTT Topics

The test uses direct MQTT topics only; it does not create Home Assistant entities.

```text
smaeenhouse/test/safe_installed_baseline/status
smaeenhouse/test/safe_installed_baseline/event
```

`status` is retained and published immediately after MQTT connection and every 30 seconds while connected. It includes WiFi/MQTT/OTA state and cumulative WiFi recovery counters (`joins`, `timeouts`, and `module_resets`), OTA poll-gap violations, safe-state enforcement count, ISR flags, and fan tach pulse count. The sketch sets the PubSubClient packet buffer to 512 bytes so this JSON payload and topic fit into one MQTT packet.

## Serial Output

When Serial is available, the sketch reports:

- WiFi connect, loss, timeout, and reconnect events
- cumulative WiFi join, connect-timeout, and NINA module-reset counters
- assigned IP address and RSSI
- OTA readiness and any poll-gap violation over two seconds
- MQTT connection state
- whether each MQTT event/status publish returned success
- safe-state status every 30 seconds, including the one-time setup enforcement count
- pending SHT/RTC ISR flags and fan tach pulse count

## Expected Observations

- The fan remains off from boot through normal operation, including after WiFi, OTA, and MQTT are active.
- The light relay remains open from boot through normal operation, including after WiFi, OTA, and MQTT are active.
- AD5263 `SHDN` remains asserted from boot through normal operation, including after WiFi, OTA, and MQTT are active.
- WiFi connects before OTA and MQTT test-topic publication starts.
- OTA remains reachable while MQTT is disconnected or reconnecting.
- After three consecutive WiFi connect timeouts, the sketch reinitializes the NINA interface and continues retrying without rebooting the SAMD21.
- WiFi reconnect attempts continue indefinitely without blocking the loop permanently.
- `ota_gap_violations` remains zero during normal network conditions.
- ISR handlers only set flags or count pulses; no I2C, MQTT, Home Assistant, or sensor logic runs inside ISRs.

## Safety Notes

This sketch is intended to be safe with the installed peripheral harness connected, but firmware is not a substitute for physical mains safety. Keep the grow-light mains wiring enclosed and isolated, and stop the test if the relay, fan, or dimmer behaves contrary to the expected safe-state polarity.

The sketch sets the safe output levels before WiFi, OTA, or MQTT are initialized. Because this test exposes no local command path, Home Assistant entity, or MQTT command that can change the actuator pins, those outputs are left untouched after setup while network services run.

## Output Capture Procedure

For manual inspection, subscribe with MQTT Explorer to:

```text
smaeenhouse/test/safe_installed_baseline/#
```

For serious runs, capture status and event messages to an ignored JSONL file under `.local/test-runs/`. Do not commit raw logs from the installed cabinet.

## Known Limitations

- The sketch validates output pin states and observable installed behavior, but it has no feedback channel proving that the relay contacts, fan power stage, or AD5263 analog path physically changed state.
- MQTT publication uses direct test topics, not Home Assistant discovery, because no production-relevant HA entity is under test at this stage.
- `shtAlertPending` and `rtcAlarmPending` are not cleared by this test; later I2C/RTC tests must evaluate and clear the corresponding hardware state in normal loop code.
- Fan tach pulses are counted only as an installed-input sanity signal; this test does not calculate RPM.

## Results And Notes For The Next Test

- Confirmation status: Passed.
- Date / firmware revision: 2026-07-15, `SafeInstalledBaseline` latest branch build at the time of the run.
- Required observations:
  - Fan stayed physically off for a multi-hour run.
  - Light relay stayed physically off/open for the same multi-hour run.
  - Status stayed normal with rising `uptime_s`, `wifi=true`, `mqtt=true`, `ota=true`, `ota_gap_violations=0`, `fan=off`, `light_relay=open`, `ad5263_shdn=asserted`, `enforce_count=1`, no SHT/RTC pending flags, and `fan_tach_pulses=0`.
  - MQTT status appeared retained under `smaeenhouse/test/safe_installed_baseline/status`.
  - OTA upload was confirmed before the long run. OTA was intentionally not retested after the long run to preserve the run duration.
- Anomalies or limitations: None recorded for the safe-state baseline.
- Safety notes to carry forward: Safe output polarity is confirmed for the next test; continue initializing non-tested actuator outputs to safe states in `setup()` and do not expose commands that can change them.
- Entity or topic notes to carry forward: Direct MQTT test topics worked; later tests should keep temporary diagnostics on direct MQTT test topics unless a production-relevant HA entity is under test.
