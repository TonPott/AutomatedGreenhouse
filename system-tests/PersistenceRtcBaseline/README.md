# Persistence RTC Baseline Test

This OTA-capable system test follows the completed I2C and SHT baselines. It validates external AT24C32 persistence and DS3231 alarm handling while the installed fan, light relay, and AD5263 dimmer remain in safe states.

The test publishes a focused set of production-relevant Home Assistant entities through the **Grow Controller Tests** device so long runs create useful Home Assistant history. Extra low-level inspection remains available through Serial output.

## Purpose

- Confirm that the installed AT24C32 can store and read a representative persistence record.
- Confirm that unchanged EEPROM bytes are skipped through `JC_EEPROM::update()`.
- Confirm that the persistence record survives reset and OTA updates.
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

The safe output levels are set once during `setup()` before EEPROM, RTC, WiFi, OTA, or MQTT are initialized. This sketch exposes no entity, MQTT command, callback, or local code path that changes those actuator pins after setup.

## EEPROM Test Record

There are no production-relevant EEPROM records yet, so this test may overwrite its documented test area starting at address `0`.

The record contains:

- magic and schema version
- sequence and boot counters
- representative persisted values for fan/light modes, fallback mode, light schedule, dim duration, SHT thresholds, and soil calibration
- checksum over the record body

At boot the sketch reads the record. If the record is missing, invalid, or has a bad checksum, it writes defaults. If the record is valid, it increments sequence and boot counters and writes the new record. Each write uses `JC_EEPROM::update()` so unchanged bytes are skipped by the library.

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
$env:SKETCH = "system-tests/PersistenceRtcBaseline"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=system-tests/PersistenceRtcBaseline ./scripts/check-arduino.sh
```

Upload over OTA from the currently running system test, or upload over USB if OTA is not available.

## Home Assistant Entities

ArduinoHA publishes these entities through the **Grow Controller Tests** device:

- `sensor.persistence_rtc_uptime_seconds`
- `sensor.persistence_rtc_wifi_joins`
- `sensor.persistence_rtc_wifi_timeouts`
- `sensor.persistence_rtc_wifi_module_resets`
- `sensor.persistence_rtc_ota_gap_violations`
- `sensor.persistence_rtc_eeprom_writes`
- `sensor.persistence_rtc_eeprom_skipped_writes`
- `sensor.persistence_rtc_eeprom_sequence`
- `sensor.persistence_rtc_eeprom_boot_count`
- `sensor.persistence_rtc_eeprom_checksum`
- `sensor.persistence_rtc_epoch`
- `sensor.persistence_rtc_alarm1_seen`
- `sensor.persistence_rtc_alarm2_seen`
- `sensor.persistence_rtc_alarm_isr_seen`
- `sensor.persistence_rtc_alarm_clears`
- `binary_sensor.eeprom_fault`
- `binary_sensor.rtc_fault`
- `binary_sensor.persistence_rtc_lost_power`
- `binary_sensor.persistence_rtc_alarm1_configured`
- `binary_sensor.persistence_rtc_alarm2_configured`
- `binary_sensor.persistence_rtc_fan_safe`
- `binary_sensor.persistence_rtc_relay_safe`
- `binary_sensor.persistence_rtc_shdn_safe`

The sketch clears the known retained direct-test status topics from earlier system-test revisions after WiFi connects and before ArduinoHA starts. Before accepting a run after entity renames or removals, also delete stale retained Home Assistant discovery/state topics from older test revisions if Home Assistant still shows obsolete entities.

## Expected Observations

- Fan remains off, light relay remains open, and AD5263 `SHDN` remains asserted.
- OTA remains reachable while MQTT is disconnected or reconnecting.
- Home Assistant history shows regular updates for the selected persistence, RTC, network, OTA, and safe-output entities.
- EEPROM sequence and boot counters increase across reset and OTA upload.
- EEPROM writes do not increase continuously during a stable long run.
- RTC Alarm1 and Alarm2 counters increase after their scheduled times.
- RTC interrupt-seen counter increases when the DS3231 SQW/INT line signals an alarm.
- No I2C, MQTT, or HA work happens in ISRs.

## Safety Notes

This test does not actuate the fan, relay, or AD5263. If any actuator changes state, stop the test and record the anomaly before creating another sketch.

## Known Limitations

- This sketch validates a test record layout only; the final production persistence schema may still evolve.
- The test may overwrite EEPROM address `0` onward because no production-relevant EEPROM records exist yet.
- The sketch does not perform NTP synchronization; it validates the current RTC time and alarm mechanics as installed.
- Controlled WiFi outage/recovery still needs to be observed manually during or after the long run.

## Results And Notes For The Next Test

- Confirmation status: Not run yet.
- Date / firmware revision: Not recorded yet.
- Required observations:
  - Confirm Home Assistant shows only the selected current entities and no stale retained entities from older test revisions.
  - Confirm EEPROM sequence and boot counters persist across reset and OTA upload.
  - Confirm EEPROM writes happen only at boot/change and skipped writes do not grow continuously in a stable loop.
  - Confirm RTC time is plausible and `lost_power` state is understood.
  - Confirm Alarm1 and Alarm2 are configured, fire, and are cleared from the main loop.
  - Confirm RTC ISR count behavior matches the alarm events.
  - Confirm OTA remains available before and after the run.
  - Confirm controlled WiFi outage/recovery counters behave as expected.
  - Confirm safe actuator outputs remain physically unchanged.
- Anomalies or limitations: Not recorded yet.
- Safety notes to carry forward: Do not proceed to SHT-driven fan behavior until persistence, RTC alarm handling, and controlled network recovery are accepted.
- Entity or topic notes to carry forward: Keep using the `Grow Controller Tests` HA device for long-run histories and remove stale retained HA topics after entity changes.
