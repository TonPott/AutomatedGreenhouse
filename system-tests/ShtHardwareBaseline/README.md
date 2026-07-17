# SHT Hardware Baseline Test

This OTA-capable system test investigates the installed SHT31 hardware after `I2cPassiveBaseline` reported no SHT acknowledgement at the old default `0x44`. The production `SHTa` implementation uses the `0x45` SHT address, so this test reports both `0x44` and `0x45` presence and uses `0x45` by default unless locally overridden.

The test does not change `SHTa.h` or `SHTa.cpp`.

## Purpose

- Publish SHT temperature and humidity measurements through direct MQTT test topics.
- Read and publish the stored SHT alert limits from the sensor.
- Monitor the SHT alert pin and report whether an interrupt was observed.
- Decode the SHT status register bits relevant to alerts, reset detection, command errors, and CRC errors.
- Preserve the confirmed safe actuator output states from earlier tests.

## Hardware Under Test

- Arduino Nano 33 IoT
- CQrobot CQRSHT31FA / SHT31-DIS-F on I2C
- SHT alert line on pin `7`
- Installed fan switch, grow-light relay, and AD5263 `SHDN` outputs in safe states
- WiFiNINA network stack
- JAndrassy ArduinoOTA with `InternalStorage`
- MQTT broker for direct test-topic publication

## Installed-System Assumptions

The safe output polarity confirmed by `SafeInstalledBaseline` remains valid:

- fan switch off = `LOW` on pin `2`
- light relay open = `LOW` on pin `3`
- AD5263 shutdown asserted = `LOW` on pin `4`

The safe output levels are set once during `setup()` before SHT, WiFi, OTA, or MQTT are initialized. This sketch exposes no entity, MQTT command, callback, or local code path that changes those actuator pins after setup.

## SHT Address

Default address:

```cpp
#define SHT31_I2C_ADDRESS SHT30_I2C_ADDR_45
```

The MQTT status includes both address probes:

- `addr.44`
- `addr.45`
- `addr.primary`

If the sensor is physically configured for `0x44`, set this in the local ignored `Credentials.h`:

```cpp
#define SHT31_I2C_ADDRESS SHT30_I2C_ADDR_44
```

## Credentials And Network Requirements

Copy `Credentials.example.h` to the ignored local file `Credentials.h`, then set WiFi, MQTT, and OTA values. Never commit `Credentials.h`.

## Upload Procedure

Compile from the repository root:

```powershell
$env:SKETCH = "system-tests/ShtHardwareBaseline"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=system-tests/ShtHardwareBaseline ./scripts/check-arduino.sh
```

Upload over OTA from the currently running system test, or upload over USB if OTA is not available.

## MQTT Topics

```text
smaeenhouse/test/sht_hardware_baseline/status
smaeenhouse/test/sht_hardware_baseline/event
```

`status` is retained and published immediately after MQTT connection and every 10 seconds while connected. It contains:

- WiFi/MQTT/OTA state, OTA poll-gap count, and cumulative WiFi recovery counters (`joins`, `timeouts`, and `module_resets`)
- safe-state summary
- SHT address probes for `0x44`, `0x45`, and the configured primary address
- temperature and humidity readings
- SHT status register raw value and decoded alert bits
- SHT alert-line level and whether the ISR has fired
- stored high/clear/low alert limits decoded to temperature and humidity

## Serial Output

When Serial is available, the sketch reports:

- SHT address presence for `0x44` and `0x45`
- temperature and humidity
- SHT status register value
- alert-line level and interrupt-seen state
- WiFi connect, loss, timeout, and reconnect events
- cumulative WiFi join, connect-timeout, and NINA module-reset counters
- OTA readiness and poll-gap warnings
- whether MQTT status/event publish returned success

## Expected Observations

- One of the two SHT addresses should report present; with the current production `SHTa` default, `0x45` is expected.
- Temperature and humidity readings should update every 2 seconds when the configured address is correct.
- Stored alert limits should be published and decoded.
- The SHT alert ISR should only set a flag; status-register evaluation happens in normal loop code.
- Fan remains off, light relay remains open, and AD5263 `SHDN` remains asserted.
- OTA remains reachable while MQTT is disconnected or reconnecting.
- After three consecutive WiFi connect timeouts, the sketch reinitializes the NINA interface and continues retrying without rebooting the SAMD21.

## Safety Notes

This test only reads the SHT sensor and its alert configuration. It does not actuate the fan, relay, or AD5263. If any actuator changes state, stop the test and record the anomaly before creating another sketch.

## Known Limitations

- This test reads stored SHT limits but does not write thresholds.
- The alert interrupt is reported as `irq_seen` once observed and is not automatically cleared back to false.
- If no SHT address responds, measurements and limits will report errors; use the address presence fields to distinguish wiring/address problems from measurement problems.

## Results And Notes For The Next Test

- Confirmation status: Not run yet.
- Date / firmware revision: Not recorded yet.
- Required observations:
  - Confirm which SHT address responds: `0x44`, `0x45`, or neither.
  - Confirm temperature and humidity readings are plausible.
  - Confirm stored high/clear/low alert limits are readable.
  - Confirm the alert line and `irq_seen` behavior during normal conditions and any alert condition that occurs naturally during the run.
  - Confirm all safe actuator states remain physically unchanged.
  - Confirm OTA upload is possible before or after the run.
- Anomalies or limitations: Not recorded yet.
- Safety notes to carry forward: Do not proceed to SHT-driven fan behavior until the SHT address, measurement reads, limit reads, and alert pin behavior are understood.
- Entity or topic notes to carry forward: Continue using direct MQTT test topics unless a production-relevant HA entity is under test.
