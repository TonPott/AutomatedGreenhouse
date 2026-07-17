# I2C Passive Baseline Test

This system test follows the completed `SafeInstalledBaseline` run. It preserves the confirmed safe actuator states, keeps OTA and direct MQTT test reporting, and adds a known-address I2C inventory check for the installed bus devices.

The test intentionally does not publish Home Assistant discovery entities. It publishes direct MQTT test status only.

## Purpose

- Confirm that the installed I2C devices coexist on the shared bus while all actuators remain safe.
- Probe only project-known addresses instead of running a broad scanner.
- Keep SHT, RTC, and fan tach ISRs minimal: ISRs only set flags or count pulses.
- Provide the carry-forward baseline before later tests initialize specific modules more deeply.

## Hardware Under Test

- Arduino Nano 33 IoT
- Installed fan switch, grow-light relay, and AD5263 `SHDN` outputs in safe states
- Installed I2C bus with:
  - SHT31 at the configured address, default `0x45`
  - DS3231 at `0x68`
  - AT24C32 at `0x57`
  - AD5263 at `0x2C`
  - TSL25911 at `0x29`
- WiFiNINA network stack
- JAndrassy ArduinoOTA with `InternalStorage`
- MQTT broker for direct test-topic publication

## Installed-System Assumptions

The full installed peripheral set may remain connected. The sketch assumes the safe output polarity already confirmed by `SafeInstalledBaseline`:

- fan switch off = `LOW` on pin `2`
- light relay open = `LOW` on pin `3`
- AD5263 shutdown asserted = `LOW` on pin `4`

The safe output levels are set once during `setup()` before I2C, WiFi, OTA, or MQTT are initialized. This sketch exposes no entity, MQTT command, callback, or local code path that changes those actuator pins after setup.

## Wiring Assumptions

| Signal | Pin | Test behavior |
|---|---:|---|
| SHT alert | `7` | `INPUT_PULLUP`, ISR sets a flag only |
| DS3231 SQW/INT | `10` | `INPUT_PULLUP`, ISR sets a flag only |
| Fan switch | `2` | Output held off by setup state |
| Fan tach | `A1` | `INPUT_PULLUP`, ISR counts pulses only |
| Light relay | `3` | Output held open by setup state |
| AD5263 `SHDN` | `4` | Output held asserted by setup state |
| Soil moisture | `A0` | Input only, no intentional reads |
| TSL25911 INT | `9` | `INPUT_PULLUP`, no ISR attached |

## Credentials And Network Requirements

Copy `Credentials.example.h` to the ignored local file `Credentials.h`, then set WiFi, MQTT, and OTA values.

## Upload Procedure

Compile from the repository root:

```powershell
$env:SKETCH = "system-tests/I2cPassiveBaseline"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=system-tests/I2cPassiveBaseline ./scripts/check-arduino.sh
```

Upload over OTA from the confirmed `SafeInstalledBaseline` test, or upload over USB if OTA is not available.

## MQTT Topics

```text
smaeenhouse/test/i2c_passive_baseline/status
smaeenhouse/test/i2c_passive_baseline/event
```

`status` is retained and published immediately after MQTT connection and every 30 seconds while connected. It contains WiFi/MQTT/OTA state, cumulative WiFi recovery counters (`joins`, `timeouts`, and `module_resets`), OTA poll-gap count, safe-state summary, I2C presence flags, probe count, last I2C result code, and ISR counters.

## Serial Output

When Serial is available, the sketch reports:

- I2C probe results every 30 seconds
- WiFi connect, loss, timeout, and reconnect events
- cumulative WiFi join, connect-timeout, and NINA module-reset counters
- assigned IP address and RSSI
- OTA readiness and any poll-gap violation over two seconds
- MQTT connection state
- whether MQTT status/event publish returned success
- compact status summaries every 30 seconds

## Expected Observations

- Fan remains off, light relay remains open, and AD5263 `SHDN` remains asserted.
- OTA remains reachable while MQTT is disconnected or reconnecting.
- After three consecutive WiFi connect timeouts, the sketch reinitializes the NINA interface and continues retrying without rebooting the SAMD21.
- Status is retained under `smaeenhouse/test/i2c_passive_baseline/status`.
- Expected devices report present at their documented addresses.
- `ota_gap_violations` remains zero during normal network conditions.
- No I2C access happens in ISRs.

## Safety Notes

This test does not actuate the fan, relay, or AD5263. It only probes the I2C addresses needed by the current project target. If any actuator changes state, stop the test and record the anomaly before creating another sketch.

## Output Capture Procedure

For manual inspection, subscribe with MQTT Explorer to:

```text
smaeenhouse/test/i2c_passive_baseline/#
```

For serious runs, capture status and event messages to an ignored JSONL file under `.local/test-runs/`. Do not commit raw logs from the installed cabinet.

## Known Limitations

- Address acknowledgement proves bus presence only; it does not validate full sensor readings, RTC time quality, EEPROM persistence, AD5263 readback, or TSL25911 measurement ranges.
- The installed SHT31 address is fixed as `I2C_ADDRESS_SHT31 = 0x45` in the sketch and is not a credential.
- The test does not clear SHT or RTC hardware interrupt causes; later module-specific tests must evaluate and clear those in loop code.

## Results And Notes For The Next Test

- Confirmation status: I2C inventory passed after correcting the documented SHT address; long-run WiFi recovery requires retest.
- Date / firmware revision: 2026-07-15, `I2cPassiveBaseline` latest branch build at the time of the run.
- Required observations:
  - Status stayed stable with rising `uptime_s`, `wifi=true`, `mqtt=true`, `ota=true`, `ota_gap=0`, and safe outputs reporting `fan=off`, `relay=open`, `shdn=asserted`, `count=1`.
  - DS3231, AT24C32, AD5263, and TSL25911 reported present.
  - SHT31 was missing while the sketch incorrectly probed `0x44`; after correcting the probe to the documented `0x45`, it became visible immediately.
  - SHT interrupt status reported `sht=false`; RTC interrupt flag reported `rtc=true` and should be handled by the later RTC test.
  - Fan tach remained `0`.
- Anomalies or limitations: MQTT updates stopped after 63,916 seconds and the board was later observed disconnected from WiFi. The run did not confirm automatic recovery. All system-test sketches now reinitialize the NINA interface after three consecutive connection timeouts and expose cumulative recovery counters; an outage/recovery and another long run are required before this network behavior is considered confirmed.
- Safety notes to carry forward: Safe actuator outputs remained unchanged; continue initializing non-tested outputs in `setup()` only.
- Entity or topic notes to carry forward: Continue using direct MQTT test topics unless a production-relevant HA entity is under test.
