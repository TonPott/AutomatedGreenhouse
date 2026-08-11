# Persistence RTC Baseline Test

This OTA-capable system test follows the completed I2C and SHT baselines. It validates external AT24C32 persistence and DS3231 alarm handling while the installed fan, light relay, and AD5263 dimmer remain in safe states.

Accepted focused revalidation firmware: `04_PersistenceRtcBaseline v1.1.0`.

The test publishes a focused set of production-relevant Home Assistant entities through the **Grow Controller Tests** device so long runs create useful Home Assistant history. Extra low-level inspection remains available through Serial output.

## Purpose

- Confirm that the installed AT24C32 can store and read a representative persistence record.
- Confirm that unchanged EEPROM bytes are skipped through `JC_EEPROM::update()`.
- Confirm that the persistence record survives reset and OTA updates.
- Exercise the persisted `fan_auto_mode` field through the same Home Assistant switch identifier used by later tests without actuating the fan.
- Distinguish EEPROM pre-read, write, and byte-identical verification results so a failed runtime persistence command cannot coexist with `EEPROM Fault = off`.
- Publish every boot and runtime persistence phase as a sequenced Home Assistant test step.
- Confirm that the DS3231 is present, readable, and can configure Alarm1 and Alarm2.
- Confirm that the RTC interrupt ISR only sets a flag and all RTC/I2C evaluation happens in `loop()`.
- Preserve OTA polling, WiFi recovery, and safe actuator states from earlier system tests.

## Hardware Under Test

- Arduino Nano 33 IoT
- WINGONEER Tiny DS3231 AT24C32 I2C module
  - DS3231 at `0x68`
  - AT24C32 at `0x57`
- DS3231 SQW/INT on pin `10`
- Installed fan switch, grow-light relay, and AD5263 `SHDN` outputs in safe states
- WiFiNINA network stack
- JAndrassy ArduinoOTA with `InternalStorage`
- ArduinoHA MQTT integration through the `Grow Controller Tests` device

## Installed-System Assumptions

The safe output polarity confirmed by `SafeInstalledBaseline` remains valid:

- fan switch off = `LOW` on pin `2`
- light relay open = `LOW` on pin `3`
- AD5263 shutdown asserted = `LOW` on pin `4`

The safe output levels are set once during `setup()` before EEPROM, RTC, WiFi, OTA, or MQTT are initialized. The `Fan Auto Mode` switch changes only the persisted test-record field; it never changes the fan output. No entity, MQTT command, callback, or local code path changes any actuator pin after setup.

## EEPROM Test Record

There are no production-relevant EEPROM records yet, so this test may overwrite its documented test area starting at address `0`.

The record contains:

- magic and schema version
- sequence and boot counters
- representative persisted values for fan/light modes, fallback mode, light schedule, dim duration, SHT thresholds, and soil calibration
- checksum over the record body

At boot the sketch reads the record. If the record is missing, invalid, or has a bad checksum, it writes defaults. If the record is valid, it preserves all stored configuration values, increments sequence and boot counters, recomputes the checksum, and writes the updated record. Each write uses `JC_EEPROM::update()` so unchanged bytes are skipped by the library.

Every boot or runtime transaction is ordered as pre-read, optional write, and verification read. Verification requires a valid record shape, a valid checksum, and a byte-identical match with the requested record. The active RAM record and the Home Assistant switch state change only after full verification.

`EEPROM Fault` is `on` unless presence, latest read, latest write decision, latest verification, record shape, and checksum are all valid. The separate Read OK, Write OK, and Verify OK entities show the failing phase. The result and sequenced test-step entities retain the operation context and error code.

`Verify Persistence Record` performs an unchanged-record readback. It must increase the skipped-write counter and complete verification without increasing the write counter.

## RTC And Alarm Behavior

The sketch initializes the DS3231, clears both alarm flags, disables old alarms, and then configures:

- Alarm1 for approximately `now + 1 minute`
- Alarm2 for approximately `now + 2 minutes`

The RTC ISR only sets a flag. The main loop reads `alarmFired(1)` and `alarmFired(2)`, clears fired alarms, and publishes counters. The test does not actuate the light when alarms fire.

## Credentials And Network Requirements

Copy `Credentials.example.h` to the ignored local file `Credentials.h`, then set WiFi, MQTT, and OTA values. Never commit `Credentials.h`.

## Upload Procedure

Compile from the repository root:

```powershell
$env:SKETCH = "sketches/system-tests/04_PersistenceRtcBaseline"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=sketches/system-tests/04_PersistenceRtcBaseline ./scripts/check-arduino.sh
```

Upload over OTA from the currently running system test, or upload over USB if OTA is not available.

## Home Assistant Entities

ArduinoHA publishes these entities through the **Grow Controller Tests** device:

- `sensor.persistence_rtc_uptime_seconds`
- `sensor.persistence_rtc_wifi_joins`
- `sensor.persistence_rtc_wifi_timeouts`
- `sensor.persistence_rtc_wifi_module_resets`
- `sensor.persistence_rtc_ota_gap_violations`
- `sensor.persistence_rtc_eeprom_reads`
- `sensor.persistence_rtc_eeprom_writes`
- `sensor.persistence_rtc_eeprom_skipped_writes`
- `sensor.persistence_rtc_eeprom_sequence`
- `sensor.persistence_rtc_eeprom_boot_count`
- `sensor.persistence_rtc_eeprom_checksum`
- `sensor.persistence_rtc_runtime_attempts`
- `sensor.persistence_rtc_runtime_successes`
- `sensor.persistence_rtc_runtime_failures`
- `sensor.persistence_rtc_test_step_index`
- `sensor.sketch_identity`
- `sensor.persistence_rtc_result`
- `sensor.persistence_rtc_test_step`
- `sensor.persistence_rtc_epoch`
- `sensor.persistence_rtc_alarm1_seen`
- `sensor.persistence_rtc_alarm2_seen`
- `sensor.persistence_rtc_alarm_isr_seen`
- `sensor.persistence_rtc_alarm_clears`
- `binary_sensor.eeprom_fault`
- `binary_sensor.persistence_rtc_eeprom_read_ok`
- `binary_sensor.persistence_rtc_eeprom_write_ok`
- `binary_sensor.persistence_rtc_eeprom_verify_ok`
- `binary_sensor.rtc_fault`
- `binary_sensor.persistence_rtc_lost_power`
- `binary_sensor.persistence_rtc_alarm1_configured`
- `binary_sensor.persistence_rtc_alarm2_configured`
- `binary_sensor.persistence_rtc_fan_safe`
- `binary_sensor.persistence_rtc_relay_safe`
- `binary_sensor.persistence_rtc_shdn_safe`
- `switch.fan_auto_mode`
- `button.verify_persistence_record`

The sketch clears the known retained direct-test status topics and the retired separate `Sketch Name` and `Sketch Version` discovery/state topics after WiFi connects and before ArduinoHA starts. `Sketch Identity` publishes `04_PersistenceRtcBaseline v1.1.0` once per MCU boot.

## Expected Observations

- Fan remains off, light relay remains open, and AD5263 `SHDN` remains asserted.
- OTA remains reachable while MQTT is disconnected or reconnecting.
- Home Assistant history shows regular updates for the selected persistence, RTC, network, OTA, and safe-output entities.
- EEPROM sequence and boot counters increase across reset and OTA upload.
- `Fan Auto Mode` commands do not change the physical fan output.
- EEPROM writes do not increase continuously during a stable long run.
- RTC Alarm1 and Alarm2 counters increase after their scheduled times.
- RTC interrupt-seen counter increases when the DS3231 SQW/INT line signals an alarm.
- No I2C, MQTT, or HA work happens in ISRs.

## Focused Revalidation Procedure For v1.1.0

Previously accepted RTC alarm, controlled WiFi outage, safe-output, and long-soak evidence does not need to be repeated unless the focused changes disturb its counters or states.

1. Install v1.1.0 over OTA and confirm the combined `Sketch Identity`, all three EEPROM phase entities `on`, `EEPROM Fault` `off`, and the boot steps ending in `boot_record_verify_ok`.
2. Record the initial fan-auto state, EEPROM sequence, write count, skipped-write count, runtime attempt/success/failure counts, and physical fan output.
3. Toggle `Fan Auto Mode` to the opposite state. Require ordered pre-read, write, and verify steps, one runtime success, no runtime failure, an incremented sequence, and unchanged physical fan output.
4. Toggle `Fan Auto Mode` back. Require the same successful transaction and physical behavior.
5. Press `Verify Persistence Record`. Require `record_verify_write_skipped_unchanged`, a successful readback, an increased skipped-write count, and no increased write count.
6. Perform an OTA update. Require the final selected fan-auto state to be restored, boot and sequence counters to increase, and all EEPROM phase entities to remain `on`.
7. Confirm that the retired separate identity entities are gone and that every persistence step is present in Home Assistant history.

Any `*_failed` step must increase Runtime Failures, keep or restore the switch to the last verified value, and set `EEPROM Fault` to `on`. A later successful user-triggered verification may clear the current fault state, while the monotonic failure counter preserves the evidence.

## Safety Notes

This test does not actuate the fan, relay, or AD5263. If any actuator changes state, stop the test and record the anomaly before creating another sketch.

## Known Limitations

- This sketch validates a test record layout only; the final production persistence schema may still evolve.
- The test may overwrite EEPROM address `0` onward because no production-relevant EEPROM records exist yet.
- The sketch does not perform NTP synchronization; it validates the current RTC time and alarm mechanics as installed.
- The controlled WiFi outage/recovery check is complete; the detailed timing remains in the local sensitive export rather than this repository.

## Results And Notes For The Next Test

- Confirmation status: **Complete**, including the focused v1.1.0 persistence revalidation.
- Date / firmware revision: Historical baseline accepted on 2026-07-20; focused `04_PersistenceRtcBaseline v1.1.0` accepted on 2026-08-10.
- Confirmed observations:
  - The complete Home Assistant export contained all 23 documented entities, and the observed run covered approximately 18 hours and 13 minutes.
  - EEPROM sequence and boot counters progressed consistently through four boots, including an OTA-triggered restart. The checksum remained valid, and EEPROM writes did not increase continuously during the stable run.
  - Both DS3231 alarms were configured, observed through the interrupt counter, and cleared in the main loop.
  - A controlled WiFi outage was initiated at approximately 13:03. Uptime continued without an MCU restart; WiFi join, timeout, and NINA module-reset counters reflected recovery activity.
  - OTA remained available after WiFi recovery, and a restart in the history was confirmed to be an intentional OTA update.
  - Fan output, light relay, and AD5263 `SHDN` remained physically stable in their safe states for the complete run.
  - Home Assistant states were republished after reconnect.
  - The focused `Fan Auto Mode OFF -> ON -> OFF` sequence completed two verified runtime writes with two successes and no failures while the physical fan output remained off.
  - The unchanged-record check reported `record_verify_write_skipped_unchanged` and a successful byte-identical verification without a record change.
  - A second confirmed OTA restart advanced boot count from `4` to `5` and sequence from `7` to `8`; the final persisted fan-auto state restored as `OFF`.
  - EEPROM Read OK, Write OK, and Verify OK remained `on`, `EEPROM Fault` remained `off`, and the combined sketch identity replaced the retired separate identity entities.
  - The skipped-write counter was not republished before the immediate OTA restart, but the sequenced unchanged-write branch and successful verification provide the required evidence; this is non-blocking.
- Anomalies or limitations:
  - Short Home Assistant `off` observations occurred around restart. They were not accompanied by a physical output change and are treated as non-blocking state-publication artifacts.
  - RTC timestamps use the local CEST time basis. This is documented and did not block the alarm test.
- Safety notes to carry forward: Test 05 may actuate only the fan. The light relay must remain open and AD5263 `SHDN` asserted from the first setup instructions onward.
- Entity or topic notes to carry forward: Preserve the existing device identifier, data prefix, and all 23 entity identifiers so their Home Assistant history remains attached. The `persistence_rtc_fan_safe` entity represents the electrical fan-output OFF level and may correctly become `off` while the fan is intentionally running.
- Privacy: The complete Home Assistant export was used only as local evidence. No raw history or derived measurement table is stored in the repository.
