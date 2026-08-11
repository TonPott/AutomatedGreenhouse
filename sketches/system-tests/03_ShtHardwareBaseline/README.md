# SHT Hardware Baseline Test

This OTA-capable system test validates the installed SHT31 at project address `0x45`. Revision `1.1.1` preserves the transaction diagnostics from revision `1.1.0`, enforces the documented SHT command spacing, and distinguishes an intentional round-trip measurement pause from a real sensor fault.

The test does not change `SHTa.h` or `SHTa.cpp`.

## Purpose

- Publish all acceptance evidence through Home Assistant using one ArduinoHA connection.
- Count measurement, status, limit, address-probe, recovery, and unchanged-limit round-trip failures separately.
- Track current and maximum consecutive sample failures instead of hiding a recovered NACK or CRC error.
- Read the four stored alert-limit registers and provide a controlled button that writes the exact same raw values back and verifies byte-identical readback.
- Stop at the first failed limit transaction. Do not issue an immediate rollback write series while the sensor or bus is unhealthy.
- Retry SHT recovery in normal loop context after a 30-second backoff and accept recovery only after a new measurement, status read, and complete limit read succeed.
- Attach the SHT alert input through `digitalPinToInterrupt(A7)` with a `FALLING` trigger and report both interrupt availability and observation.
- Decode the SHT status register bits relevant to alerts, reset detection, command errors, and CRC errors.
- Preserve the confirmed safe actuator output states from earlier tests.

## Hardware Under Test

- Arduino Nano 33 IoT
- CQrobot CQRSHT31FA / SHT31-DIS-F on I2C
- SHT alert line on `A7` (`PB03` / `EXTINT3`)
- Installed fan switch, grow-light relay, and AD5263 `SHDN` outputs in safe states
- WiFiNINA network stack
- JAndrassy ArduinoOTA with `InternalStorage`
- MQTT broker with Home Assistant discovery

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

Home Assistant exposes the expected `0x45` address state and a cumulative address-probe failure counter. Address `0x44` remains a boot diagnostic only because it is not the configured project address.

## Credentials And Network Requirements

Copy `Credentials.example.h` to the ignored local file `Credentials.h`, then set WiFi, MQTT, and OTA values. Never commit `Credentials.h`.

## Upload Procedure

Compile from the repository root:

```powershell
$env:SKETCH = "sketches/system-tests/03_ShtHardwareBaseline"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=sketches/system-tests/03_ShtHardwareBaseline ./scripts/check-arduino.sh
```

Upload over OTA from the currently running system test, or upload over USB if OTA is not available.

## Home Assistant Evidence

Test 03 uses the established Grow Controller Tests device identifier and data prefix. It does not open a second direct MQTT connection. The obsolete retained direct status topic from older Test 03 builds and retained discovery/state topics for `sensor.sketch_name` and `sensor.sketch_version` are cleared through the ArduinoHA connection.

`sensor.sketch_identity` publishes `03_ShtHardwareBaseline v1.1.1` once per MCU boot. Relevant states are republished after an MQTT reconnect.

The focused entities are:

- `sensor.temperature`
- `sensor.humidity`
- `binary_sensor.sht_fault`
- `binary_sensor.sht_address_45`
- `binary_sensor.sht_measurement_ok`
- `binary_sensor.sht_status_ok`
- `binary_sensor.sht_limits_ok`
- `binary_sensor.sht_alert_line`
- `binary_sensor.sht_interrupt_attached`
- `sensor.sht_measurement_errors`
- `sensor.sht_status_errors`
- `sensor.sht_limit_errors`
- `sensor.sht_address_probe_failures`
- `sensor.sht_consecutive_failures`
- `sensor.sht_max_consecutive_failures`
- `sensor.sht_recovery_attempts`
- `sensor.sht_recoveries`
- `sensor.sht_recovery_failures`
- `sensor.sht_round_trip_attempts`
- `sensor.sht_round_trip_successes`
- `sensor.sht_round_trip_failures`
- `sensor.sht_status_register`
- `sensor.sht_status_flags`
- `sensor.sht_limit_high_set_raw`
- `sensor.sht_limit_high_clear_raw`
- `sensor.sht_limit_low_set_raw`
- `sensor.sht_limit_low_clear_raw`
- `sensor.sht_last_error`
- `sensor.sht_round_trip_result`
- `sensor.sht_test_step_index`
- `sensor.sht_test_step`
- `button.run_sht_limit_round_trip`

The inherited uptime, WiFi recovery, OTA-gap, and safe-output entities are also republished. Boot steps are buffered until HA connects. Each later action publishes a complete state snapshot and then publishes the sequenced `sht_test_step` value as its commit marker.

## SHT Command Sequencing

Boot diagnostics run while the SHT is idle: address probe, stop, soft reset, status read, four-register limit read, and only then periodic measurement start. The first measurement is deferred until the normal two-second sampling interval has elapsed.

Direct SHT transactions wait at least 1 ms before the next command or response phase. A failed transaction is still counted and published; command spacing is not implemented as a hidden retry.

## Unchanged-Limit Round Trip

The button is accepted only after at least five consecutive healthy measurement/status cycles and a successful four-register limit read. It performs:

1. Check the healthy preconditions, stop periodic measurement, and verify the library result.
2. Read all four limit registers while the sensor is idle and copy their raw values in RAM.
3. Write high-set, high-clear, low-set, and low-clear with their unchanged raw values, aborting after the first failed transaction.
4. Read the four registers back and require exact raw equality.
5. Restart periodic measurement once.
6. Continue the normal measurement/status/limit soak.

Every path after a successful stop attempts exactly one periodic restart. The controlled pause does not assert `binary_sensor.sht_fault`; a capture, write, readback, or restart failure still asserts the fault through its error state or scheduled recovery.

No new threshold value is calculated and no rollback series is required because every attempted write contains the value read from that same register immediately beforehand.

## Focused Rerun Procedure

1. Upload revision `1.1.1` and confirm the combined identity plus the removal of the two retired identity entities.
2. Confirm successful idle-state status and limit reads, then wait for at least five healthy samples. No status, measurement, limit, or recovery counter may rise during a healthy startup.
3. Press `Run SHT Limit Round Trip` three times, with at least 30 seconds of normal measurement between runs.
4. Require `round_trip_passed` for every run, `SHT Fault=off` throughout each successful transaction, unchanged plausible temperature/RH, and no unexplained counter increase.
5. Continue a 30-minute soak. A single recovered transaction fault remains evidence and must be reported; three consecutive failed sample cycles must schedule recovery.
6. Confirm OTA still succeeds after the round trips. Previously accepted WiFi-outage and actuator-safe-state tests do not need to be repeated unless their counters or states become abnormal.

## Serial Output

Serial remains optional and is not required for acceptance. When available, it reports:

- SHT address presence for `0x44` and `0x45`
- temperature and humidity
- SHT status register value
- alert-line level, interrupt-attached state, and interrupt-seen state
- WiFi connect, loss, timeout, and reconnect events
- cumulative WiFi join, connect-timeout, and NINA module-reset counters
- OTA readiness and poll-gap warnings

## Expected Observations

- Address `0x45` reports present and `SHT Address Probe Failures` does not rise during a healthy run.
- Boot status and alert-limit diagnostics complete before periodic measurement starts.
- The first temperature/RH sample arrives through the normal two-second sampling path rather than a forced setup transaction.
- Temperature and humidity readings should update every 2 seconds when the configured address is correct.
- Stored alert limits remain readable before and after every unchanged-limit round trip.
- Successful round trips do not pulse `SHT Fault`; any real transaction or restart failure remains visible.
- The combined `Sketch Identity` value is correct and the retired separate identity entities disappear.
- `SHT Interrupt Attached` is `on` for `A7`. The ISR only sets a flag; all Wire and HA work remains in normal loop context.
- Fan remains off, light relay remains open, and AD5263 `SHDN` remains asserted.
- OTA remains reachable while MQTT is disconnected or reconnecting.
- After three consecutive WiFi connect timeouts, the sketch reinitializes the NINA interface and continues retrying without rebooting the SAMD21.

## Safety Notes

This test never actuates the fan, relay, or AD5263. Its only writes target the four SHT alert-limit registers and reproduce their previously read raw values exactly. If any actuator changes state, stop the test and record the anomaly.

## Known Limitations

- The button verifies unchanged register writes; changing threshold semantics remains Test 05 scope.
- The alert interrupt is reported as `irq_seen` once observed and is not automatically cleared back to false.
- Recovery is intentionally rate-limited. Persistent failures remain visible rather than causing a tight reset or write loop.
- One OTA poll-gap violation during the initial Home Assistant publication is accepted for this revision and is not part of the SHT correction.

## A7 Interrupt Validation

The installed SHT ALERT wire remains on `A7`. A valid run must report `SHT Interrupt Attached=on`. Trigger a real SHT alert, confirm the active-low line, and observe the sequenced `sht_alert_interrupt_seen` step. A missing interrupt mapping or an alert transition visible only through polling fails this supplemental pin validation.

The historical baseline results below predate the `A7` wiring change. They remain historical evidence; the focused revision validates the current interrupt path again.

## Results And Notes For The Next Test

- Confirmation status: Reopened for revision `1.1.1` startup sequencing, round-trip fault semantics, and soak validation.
- Revision `1.1.0` evidence: three unchanged-limit round trips passed, but startup produced one recovered `status_read error=527` and every successful round trip briefly pulsed `SHT Fault`. Revision `1.1.1` addresses those two observations without hiding real errors.
- Required new evidence: three successful unchanged-limit round trips, 30-minute post-write soak, correct sequenced HA steps, combined identity cleanup, and OTA after the round trips.
- Historical status: The original read-only baseline was complete. The installed SHT31 was stable at fixed project address `0x45`; `0x44` did not respond on this hardware.
- Historical firmware revision: Follow-up branch build containing the fixed `I2C_ADDRESS_SHT31 = SHT30_I2C_ADDR_45` constant.
- Required observations:
  - Address probes reported `addr.44=false`, `addr.45=true`, and `primary=0x45`.
  - The test stayed online through at least `uptime_s=183537` with `wifi=true`, `mqtt=true`, `ota=true`, and `ota_gap=0`.
  - Network counters reported `joins=1`, `timeouts=1`, and `module_resets=0` during the accepted run.
  - Measurements were valid and plausible at the last captured status: `t=26.80`, `rh=46.8`, `err=0`, and `samples=91529`.
  - Stored SHT alert limits were readable through `reads=6117` with `err=0`; the decoded high set, high clear, low set, and low clear values were published successfully.
  - A later focused run showed one transient measurement error (`err=268`) at `uptime_s=412`; the following captured sample was valid again at `uptime_s=612`, and OTA still worked after approximately 770 seconds. The event did not cause a network disconnect.
  - The status register read succeeded with `raw=32816`, `alert=true`, `rh_alert=false`, `temp_alert=false`, `reset=true`, `cmd_err=false`, `crc_err=false`, `line_low=false`, and `irq_seen=false`.
  - Safe actuator outputs remained reported as `fan=off`, `relay=open`, `shdn=asserted`, `count=1`.
- Anomalies or limitations: The status register showed the SHT alert summary bit set while the decoded RH and temperature alert bits were false and the alert line stayed high. Carry this forward as a status-decoding or latched-status item to re-check before relying on the SHT alert to drive the fan. The isolated measurement error recovered without sensor reinitialization; later tests must count and expose transient SHT transaction errors instead of treating one recovered error as a reason to restart networking or the sensor.
- Safety notes to carry forward: Do not energize the fan automatically until the next fan test explicitly writes thresholds, clears/understands any latched SHT status, verifies alert-line behavior, and confirms tach feedback. Keep relay and AD5263 outputs safe in setup because they are not part of the fan test.
- Entity or topic notes to carry forward: The SHT-driven fan test should expose the production-relevant fan entities plus compact SHT error counters and sequenced test events through Home Assistant. Avoid a second direct-MQTT client when the same acceptance evidence can be represented by the existing ArduinoHA connection.
