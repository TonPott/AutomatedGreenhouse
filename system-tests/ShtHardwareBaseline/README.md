# SHT Hardware Baseline Test

This OTA-capable system test investigates the installed SHT31 hardware after `I2cPassiveBaseline` reported no SHT acknowledgement at the old default `0x44`. The production `SHTa` implementation and installed hardware use the `0x45` SHT address, so this test reports both `0x44` and `0x45` presence and uses the fixed project address `0x45`.

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

Project address:

```cpp
constexpr uint8_t I2C_ADDRESS_SHT31 = SHT30_I2C_ADDR_45;
```

The address is a hardware constant in the sketch, not a credential or local configuration value.

The MQTT status includes both address probes:

- `addr.44`
- `addr.45`
- `addr.primary`

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

- Confirmation status: Complete. The installed SHT31 is stable at the fixed project address `0x45`; `0x44` does not respond on this hardware.
- Date / firmware revision: Follow-up branch build containing the fixed `I2C_ADDRESS_SHT31 = SHT30_I2C_ADDR_45` constant.
- Required observations:
  - Address probes reported `addr.44=false`, `addr.45=true`, and `primary=0x45`.
  - The test stayed online through at least `uptime_s=183537` with `wifi=true`, `mqtt=true`, `ota=true`, and `ota_gap=0`.
  - Network counters reported `joins=1`, `timeouts=1`, and `module_resets=0` during the accepted run.
  - Measurements were valid and plausible at the last captured status: `t=26.80`, `rh=46.8`, `err=0`, and `samples=91529`.
  - Stored SHT alert limits were readable through `reads=6117` with `err=0`; the decoded high set, high clear, low set, and low clear values were published successfully.
  - The status register read succeeded with `raw=32816`, `alert=true`, `rh_alert=false`, `temp_alert=false`, `reset=true`, `cmd_err=false`, `crc_err=false`, `line_low=false`, and `irq_seen=false`.
  - Safe actuator outputs remained reported as `fan=off`, `relay=open`, `shdn=asserted`, `count=1`.
- Anomalies or limitations: The status register showed the SHT alert summary bit set while the decoded RH and temperature alert bits were false and the alert line stayed high. Carry this forward as a status-decoding or latched-status item to re-check before relying on the SHT alert to drive the fan. A controlled WiFi outage/recovery test is also still needed even though this long run remained connected.
- Safety notes to carry forward: Do not energize the fan automatically until the next fan test explicitly writes thresholds, clears/understands any latched SHT status, verifies alert-line behavior, and confirms tach feedback. Keep relay and AD5263 outputs safe in setup because they are not part of the fan test.
- Entity or topic notes to carry forward: The next SHT-driven fan test may introduce only the production-relevant fan HA entities needed for manual fan control, fan auto mode, RPM, and `fan_fault`; extra diagnostics should remain under direct MQTT test topics.
