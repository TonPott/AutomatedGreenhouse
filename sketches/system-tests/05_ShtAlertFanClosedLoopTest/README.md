# SHT Alert Fan Closed-Loop Test

This OTA-capable system test extends the completed persistence/RTC baseline with SHT31 alert evaluation, manual fan control, automatic high-alert fan demand, RPM monitoring, and fan-fault reporting. It intentionally leaves the grow-light relay open and the AD5263 dimmer in hardware shutdown.

The test reuses the Home Assistant device identifier and data prefix from Test 04. Its 23 existing entities therefore keep their history, while 25 SHT, fan, identity, error-counter, test-step, and command-feedback entities are added. Version 1.1.4 uses only the ArduinoHA MQTT connection and removes retained topics left by the former direct diagnostics, separate identity entities, and Test-04-only diagnostic entities.

## Status

Test 05 remains **Reopened** until version `1.1.4` passes the focused hardware procedure below. The functional results accepted on 2026-07-21 remain valid: manual and automatic fan behavior, RPM, tach-fault recovery, low-alert neutrality, WiFi recovery, and RTC operation do not need to be repeated. This revision isolates A7 alert-edge validation with the fan held off and revalidates the corrected SHT and EEPROM transaction paths.

## Purpose

- Verify that all actuator outputs enter safe electrical states before any other initialization.
- Verify direct SHT31 measurement, status, alert-limit write, and limit readback behavior without changing production `SHTa.*`.
- Verify that only temperature or humidity high alerts create automatic fan demand.
- Detect and record low alerts without starting the fan.
- Verify manual fan operation independently of SHT faults and automatic mode.
- Verify the test-local fan controller, including inverted tach input, two pulses per revolution, a full 30-second window starting when the fan starts, raw tach-pulse observability, and the 30-second fault grace period.
- Preserve Test 04 EEPROM, RTC alarm, WiFi recovery, OTA, and Home Assistant republish behavior.

## System-Test Lineage

Runtime behavior is carried forward only from system tests: SHT measurement/status behavior from `03_ShtHardwareBaseline` and persistence, RTC, WiFi, OTA, MQTT, and Home Assistant behavior from `04_PersistenceRtcBaseline`. No earlier hardware-test sketch is used as an implementation source. Test-local fan instrumentation was added in response to the installed Test 05 observation that the fan rotated while RPM remained zero.
## Hardware Under Test

- Arduino Nano 33 IoT
- SHT31 at project address `0x45`, alert output on `A7` (`PB03` / `EXTINT3`)
- Fan switch on pin `2`
- Fan tachometer through the documented inverting 2N3904 conditioning stage on `A1`
- WINGONEER Tiny DS3231/AT24C32 module
  - DS3231 at `0x68`, SQW/INT on pin `10`
  - AT24C32 at `0x57`
- Grow-light relay on pin `3`, held open
- AD5263 `SHDN` on pin `4`, held asserted
- WiFiNINA, ArduinoOTA with `InternalStorage`, MQTT, and ArduinoHA

The project schematic is the primary wiring reference. Never disconnect fan power or obstruct the fan for the tach-fault test; disconnect only the conditioned tach signal while the fan rotates freely.

## Safe Boot And Runtime Behavior

The first setup operations configure and assert:

- fan output `LOW` (off)
- light relay `LOW` (open)
- AD5263 `SHDN` `LOW` (asserted)

These levels are set before Serial, Wire, EEPROM, RTC, SHT, fan-controller, WiFi, OTA, MQTT, or Home Assistant initialization. The fan remains off until SHT initialization, threshold write/readback, and the first valid status evaluation have finished. The light relay and `SHDN` have no command or callback that can change them during this test.

The existing `binary_sensor.persistence_rtc_fan_safe` is displayed as **Fan Output Off**. It reports only whether the electrical fan output is at the OFF level. It must become `off` while the fan is intentionally running and is not itself a fault indication.

## Persistence And Reset Behavior

The version-1 Test 04 record remains at EEPROM address `0`. Its format is unchanged. Every boot and runtime persistence operation uses pre-read, optional byte-wise `JC_EEPROM::update()`, and byte-identical readback verification.

- Sequence and boot count advance from the existing record at boot.
- `fan_auto_mode` is persisted in that record when changed.
- A failed boot read does not trigger a default write. Defaults are written only after a successful read proves that the stored structure or checksum is invalid.
- The active RAM record changes only after presence, read, write-or-skip, readback, structure, and checksum checks all pass.
- A failed `fan_auto_mode` transaction keeps the last verified RAM state active and republishes the Home Assistant switch accordingly.
- The manual fan switch is volatile and always starts `OFF`.
- Temperature and humidity high set/clear values start from the stored Test 04 values.
- Low set/clear values start from the safe production defaults.
- All eight threshold changes made through Home Assistant are RAM-only. Reset or OTA therefore restores the stored high values and safe low defaults.
- `EEPROM Fault` remains `on` unless the complete latest transaction and the verified record are valid.

Both RTC alarms are still configured and handled as in Test 04. The RTC ISR only sets a flag; alarm reads and clears occur in the main loop.

## SHT Alert Semantics

The test uses the Sensirion SHT3x API and test-local direct alert-register commands. Production `SHTa.h` and `SHTa.cpp` are not changed.

For each temperature and humidity channel:

- High demand is set only when the channel tracking bit is active and the measured value has reached its high-set threshold.
- High demand remains set until the measured value reaches or falls below high-clear.
- A low tracking alert is recorded when the tracking bit is active and the value reaches or falls below low-set.
- Low tracking never creates fan demand.
- Any measurement, status, command/CRC, threshold-write, or readback failure forces automatic demand off.
- Manual fan control remains available after switching `fan_auto_mode` off.
- A detected SHT reset bit causes all active limits to be written and verified again, followed by status clear and a fresh status read in the main loop.

Version `1.1.4` enforces the transaction spacing validated by Test 03 through one central microsecond guard. Successive SHT commands are separated by at least `1 ms`, including measurement fetch followed by status read and the command/read phases of direct limit-register operations. Boot initialization is ordered as:

1. probe the configured address
2. stop periodic acquisition
3. perform a soft reset
4. capture status and all four limits while idle
5. clear status
6. write and read back all four active limits
7. clear status again
8. restart periodic measurement

The first measurement is requested only after the normal two-second interval. A controlled limit transaction does not create a false `SHT Fault` merely because periodic measurement is paused. Write, readback, restart, measurement, status, CRC, command, or recovery failures remain visible.

Every valid runtime limit command stops periodic measurement, aborts at the first failed operation, and makes exactly one restart attempt after a successful stop. A failed bus transaction does not issue an immediate rollback series. The previous RAM configuration remains selected, automatic demand is forced off, and recovery reapplies it after a 30-second backoff.

Version `1.1.4` validates the installed Nano 33 IoT pin descriptor before registering `attachInterrupt(digitalPinToInterrupt(A7), onShtAlert, RISING)`. `A7` maps to `PB03` / `EXTINT3`. The SHT31 ALERT output is push-pull and active high, so the pin uses `INPUT` without an internal pull-up. The ISR only sets a flag; SHT status, measurements, MQTT, and Home Assistant work remain in the loop.

For this focused revision, keep `Fan Auto Mode=OFF` and `Fan=OFF` while producing two temperature-high alert cycles. The alert line, tracking bit, automatic-demand diagnostics, and A7 ISR counter must react, but the effective fan output must remain off. Each inactive-to-active transition must be counted as a separate rising edge.

If the earlier unexplained summary-bit state appears again while neither tracking bit is active and the alert line is not active, stop the acceptance run and preserve the raw status shown in `SHT Diagnostic`.

## Threshold Validation

Home Assistant accepts a threshold command only when the complete resulting tuple is valid:

```text
temperature: low_set < low_clear < high_clear < high_set
humidity:    low_set < low_clear < high_clear < high_set
```

Every adjacent temperature value must be at least `0.5 C` apart. Every adjacent humidity value must be at least `1.0 % RH` apart. Temperature is limited to `-40..125 C`; humidity is limited to `0..100 % RH`.

After every valid command, all four packed SHT limit registers are written and all four are read back. Acceptance tolerances account for SHT register quantization:

- temperature: at most `0.4 C` deviation
- humidity: at most `0.8 % RH` deviation

An invalid command is rejected, the previous Home Assistant value is republished, and a test step is queued. `sensor.sht_threshold_result` reports an incrementing applied/rejected/failed result for every command, including the affected entity, requested value, and readback or restart error. If apply or readback fails, automatic demand is forced off and the previous complete RAM tuple remains selected for delayed recovery.

When moving a high pair downward, change `high_clear` first and then `high_set`. When moving it upward, change `high_set` first and then `high_clear`. This keeps the complete tuple valid after every individual Home Assistant command.

## Home Assistant Entities

The entity limit is `48`. The 23 Test 04 entities and their identifiers remain unchanged. Test 05 adds 25 entities:

- `sensor.temperature`
- `sensor.humidity`
- `sensor.fan_rpm`
- `sensor.fan_tach_pulses`
- `sensor.sht_alert_interrupts`
- `sensor.sht_measurement_errors`
- `sensor.sht_status_errors`
- `sensor.sketch_identity`
- `sensor.sht_threshold_result`
- `sensor.sht_diagnostic`
- `sensor.test_event`
- `binary_sensor.fan_fault`
- `binary_sensor.sht_fault`
- `binary_sensor.sht_alert_line`
- `binary_sensor.sht_interrupt_attached`
- `switch.fan`
- `switch.fan_auto_mode`
- `number.temp_high_set`
- `number.temp_high_clear`
- `number.temp_low_set`
- `number.temp_low_clear`
- `number.hum_high_set`
- `number.hum_high_clear`
- `number.hum_low_set`
- `number.hum_low_clear`

`Sketch Identity` publishes `05_ShtAlertFanClosedLoopTest v1.1.4` once per MCU boot. `Test Step` retains the existing `sensor.test_event` identifier and publishes each transition with a monotonically increasing sequence. Up to 24 steps are buffered while MQTT is unavailable, and at most one queued step is published per loop pass so OTA and local control continue to be serviced.

The 30-second HA refresh calls entity setters without forcing unchanged values. A new MQTT connection forces the complete numeric, binary, switch, and number state set once. The physical SHT alert-line entity is also updated on state changes from loop context. The SHT interrupt counter is published at the normal HA interval, and the static interrupt-attached state is published at boot/reconnect. No MQTT operation occurs in an ISR.

`switch.fan` is accepted only while `switch.fan_auto_mode` is `OFF`. A manual command received in auto mode is rejected, the manual request is cleared, the switch is forcibly republished as `OFF`, and a test event is emitted. Enabling auto mode also clears any earlier manual request.

`Fan RPM` remains zero until the first complete 30-second window after the effective fan start. `Fan Tach Pulses` is a boot-total pulse counter that distinguishes missing tach interrupts from an RPM-window timing issue.

The sketch maintains only the ArduinoHA MQTT connection. It clears retained state for the removed direct Test 04/05 diagnostic topics, the separate `sketch_name` and `sketch_version` entities, and the eleven Test-04-only read/write/result diagnostic entities. Cleanup uses the existing ArduinoHA MQTT connection; no second MQTT client is created.

SHT measurement and status error counters are monotonic for the current boot. A single transient error forces automatic demand off for that cycle. Repeated measurement or status errors, transaction failures, reset detection, and CRC/command status bits schedule the documented recovery path.

All MQTT, Home Assistant, SHT, RTC, EEPROM, and I2C work runs in normal loop context. The SHT and RTC ISRs set flags only, and the fan tach ISR only increments counters.

## Credentials And Compile Check

Copy `Credentials.example.h` to the ignored local `Credentials.h` for upload. Never commit real credentials.

Compile from the repository root:

```powershell
$env:SKETCH = "sketches/system-tests/05_ShtAlertFanClosedLoopTest"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=sketches/system-tests/05_ShtAlertFanClosedLoopTest ./scripts/check-arduino.sh
```

The compile script may create `Credentials.h` from the example only when none exists and must remove that temporary file afterward.

## Focused v1.1.4 Acceptance Procedure

Previously accepted manual fan, automatic fan, RPM, tach-fault, low-alert, WiFi-recovery, and RTC results do not need to be repeated.

1. Compile with `SKETCH=sketches/system-tests/05_ShtAlertFanClosedLoopTest`; confirm credential cleanup and the minimal A7, RTC, and tach ISR bodies.
2. Install by OTA. Require identity `05_ShtAlertFanClosedLoopTest v1.1.4`, the complete sequenced boot path, successful EEPROM read/write-or-skip/verify, `EEPROM Fault=off`, verified SHT limits, and the first SHT measurement only after the regular two-second interval.
3. With no active alert, switch `Fan Auto Mode` to `ON` and back to `OFF`. Both operations must report pre-read, write, and verify, increase sequence/write counters, produce no fault, and leave the effective fan output off.
4. Keep `Fan Auto Mode=OFF` and `Fan=OFF`. Produce two temperature-high cycles. When lowering the pair, set `high_clear` before `high_set`; when raising it, set `high_set` before `high_clear`. Require the active-high alert line, temperature tracking bit, auto-demand steps, and A7 ISR count to react while the effective fan remains off. Both rising edges must be counted separately.
5. Restore safe limits and run for at least 20 minutes, beyond the earlier approximately 770-second failure point. Reject SHT or EEPROM faults, CRC/command bits, unexplained availability loss, or unexplained OTA poll gaps.
6. Perform a final OTA update. Require `fan_auto_mode=OFF`, increased boot and sequence values, restored stored high limits and safe low defaults, safe outputs, and successful EEPROM/SHT boot transactions. The availability gap caused by the confirmed OTA is expected.

## Safety And Privacy

- Stop immediately if the light relay closes, `SHDN` releases, the fan does not rotate freely when commanded, or an output differs from its documented electrical level.
- Never stop or obstruct the fan to inject a tach fault.
- Restore safe limits before OTA or leaving the test unattended.
- Do not commit Home Assistant exports, raw measurement histories, timestamped cabinet data, or derived real-environment datasets. Keep any local evidence in an ignored measurement/export directory.

## Known Limitations

- The production `SHTa` API still reports channel tracking but does not distinguish high and low causes. This test-local evaluator validates the intended high-only behavior before any production API change is planned.
- Threshold commands are intentionally volatile except for the previously stored Test 04 high values.
- The fan tachometer has no fixed nominal RPM range yet.
- The historical `D7` run could validate alert behavior only through loop polling. The `A7` / `EXTINT3` interrupt path remains pending until the focused v1.1.4 hardware run above passes.
- The functional tach-fault test disconnected and restored the complete fan connector rather than only the conditioned tach line. This proves command-on/no-pulses fault detection and recovery after reconnection, but not the narrower rotating-fan/tach-wire-open electrical case.

## Results And Notes For The Next Test

- Confirmation status: **Reopened** until the focused v1.1.4 SHT, EEPROM, A7, soak, and final-OTA procedure passes.
- Date / firmware revision: Version `1.1.0` functional results accepted on 2026-07-21; corrective firmware `1.1.4` implemented on 2026-08-10.
- Required observations: Manual `OFF -> ON -> OFF` control passed with auto mode disabled. Manual fan commands were rejected while auto mode was enabled. Temperature and humidity high-alert cycles produced and cleared automatic demand correctly; separate temperature/humidity low tracking remained fan-neutral. Threshold application/rejection feedback, temperature/humidity values, and SHT health were visible. RPM settled repeatedly near `804..809 rpm` after the documented first-window delay. During fault injection the output command remained on, RPM moved from approximately `807` through a partial-window `15` to `0`, `fan_fault` turned on, then RPM recovered through approximately `620` to `805` and the fault cleared automatically. Uptime remained monotonic.
- Anomalies or limitations: The complete fan connector was unplugged and reconnected instead of disconnecting only the conditioned tach signal while leaving the fan powered. This is accepted as the functional fault/recovery result, but does not isolate the tach conditioning path. RPM changes are intentionally delayed by the 30-second measurement window. The A7 interrupt path still requires the focused bench confirmation above.
- Safety notes to carry forward: Never block the fan. Preserve relay-open and AD5263-`SHDN` safe states. Keep both fan switches off during the A7 cycle check.
- Entity or topic notes to carry forward: Preserve the Test 04 device identifier/data prefix and all 48 Test 05 entity IDs. Version `1.1.4` keeps the combined once-per-boot identity and clears obsolete retained discovery/state through the existing ArduinoHA MQTT connection.
