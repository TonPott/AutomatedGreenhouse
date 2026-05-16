Repository documentation is currently maintained in English. Implement code, identifiers, comments, and commit summaries in English only.

## Purpose

This file defines mandatory working rules for Codex and other AI agents in this repository.

## Project Context

This is an Arduino-IDE-compatible firmware project for an **Arduino Nano 33 IoT**.
The firmware controls sensors, fan, light, RTC, external EEPROM, and Home Assistant MQTT integration.

## Current Project Phase

The repository is in a migration phase based on existing firmware.

- Existing code is refactored and migrated deliberately.
- Do not keep parallel legacy paths as runtime options unless they are explicitly documented.
- The old PWM/RC/PC817 light path is no longer a valid target concept.
- The current target path for light dimming is the AD5263BRUZ50 resistance path.
- Authoritative sources for the migration are:
  - `SPEC.md`
  - `MODULES.md`
  - `docs/entity-model.md`
- If existing code and current documentation contradict each other, the current documentation takes precedence.
- Persistence is extended deliberately without creating existing configuration values a second time.
- Resume state and fault states are part of the mandatory migration scope.

## Mandatory Working Rules

### 1. Keep The Project Structure
- The project remains a classic Arduino sketch folder.
- The main sketch is located at `Smaeenhouse/Smaeenhouse.ino`.
- Header and CPP files remain in the same folder as the main sketch unless explicitly requested otherwise.

### 2. Never Overwrite Credentials
- `Credentials.h` is local and secret.
- Never write real credentials to the repository.
- Only `Credentials.example.h` may be changed as a template.

### 3. No I²C Logic In ISRs
- ISRs may only set flags or count pulses.
- No Wire/I²C communication in ISRs.
- No sensor reads in ISRs.
- This explicitly also applies to:
  - SHT alert
  - DS3231 SQW/INT
  - external AT24C32 EEPROM access
  - AD5263 access

### 4. No MQTT/HA Logic In ISRs
- MQTT, ArduinoHA, and publishes only in the main loop or normal methods.
- ISRs remain minimal.

### 5. Strictly Separate Light Logic
There are two control worlds:
- `light_auto_mode = ON` → Arduino schedule active, HA schedule ignored
- `light_auto_mode = OFF` → HA control active, Arduino schedule ignored

This separation must not be weakened.

### 6. HA Dimming Job Only Through The Defined Entities
Timed HA dimming jobs are modeled exclusively through these entities:
- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

Do not introduce alternative JSON/string commands unless the user explicitly requests them.

### 7. Persistence Through External RTC EEPROM
- Use the **AT24C32 EEPROM** of the WINGONEER Tiny DS3231 AT24C32 I2C module.
- Do not use `SAMD_SafeFlashStorage` persistence anymore.
- Use the **JC_EEPROM** library for the external EEPROM.
- Encapsulate EEPROM access in a project-internal persistence layer.
- Write only when values actually changed.
- Avoid unnecessarily frequent writes during normal runtime.
- Extend persistence deliberately with resume state without creating existing values a second time.

### 8. Use RTC Alarms For The Arduino-Internal Light Schedule
- Use both DS3231 alarms for the internal Arduino light schedule.
- `Alarm1` and `Alarm2` represent the stored on/off or dim-on/dim-off times.
- The times from persisted configuration are written to the DS3231 alarm registers after boot, time sync, and configuration changes.
- The SQW/INT output of the DS3231 is connected to `PIN_RTC_ALARM`.
- The ISR only sets a flag; evaluation happens in the main loop through RTClib.

### 9. Do Not Add Libraries Unnecessarily
Do not introduce new libraries if:
- existing project libraries are sufficient
- the function can be implemented with standard Arduino means

If a new library is still needed:
- document the reason in the change
- update `libraries.txt`

### 10. Republish Home Assistant States Cleanly
After startup or MQTT reconnect, these must be published again to HA:
- light state
- switch states
- number configuration values
- relevant sensor values

### 11. FanController Remains Dumb Regarding Thresholds
- FanController must not evaluate its own temperature/RH thresholds.
- The decision comes exclusively from the SHT alert logic.

### 12. Soil Moisture Calibration Remains HA-Controlled
- Do not invent an internal calibration routine in the firmware.
- Firmware provides raw values and stores final calibration data.
- HA guides the user through the routine.
- Do not introduce additional soil capture buttons such as `capture_soil_air` or `capture_soil_water` unless explicitly requested.

### 13. Align Changes With Documentation First
Before larger code changes:
- consider `SPEC.md`
- consider `MODULES.md`
- consider `docs/entity-model.md`
- consider the existing schematic in the project folder

### 14. Use SHTa Routines
- `SHTa.h/.cpp` should continue to be used
- ask before changing them and provide a reason.

### 15. Keep Documentation Understandable For New Users
- Hardware sections should be written so the project is understandable on first read.
- Mention the schematic in the project folder as the primary reference.
- Text documentation should also describe signal conditioning for:
  - light dimmer (AD5263 resistance path between `Dim+` and `Dim-`)
  - fan tachometer (2N3904 stage)

If code and documentation contradict each other, documentation comes first, unless the user explicitly says otherwise.

## Preferred Working Order

1. Fix compile errors first
2. Then clean up state logic
3. Then complete HA entity mapping
4. Then polish / logging / debugging

## Not Wanted

- large logic blocks directly in `Smaeenhouse/Smaeenhouse.ino`
- hidden global side effects
- blocking `delay()` chains
- vendoring/checking in libraries without a reason
- spontaneous changes to the repository structure
