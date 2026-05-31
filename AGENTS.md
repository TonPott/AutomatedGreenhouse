Repository documentation is currently maintained in English. Implement code, identifiers, comments, documentation changes, and commit summaries in English only.

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
  - `HARDWARE.md`
  - `docs/entity-model.md`
- For open work and validation tasks, consult `ROADMAP.md`.
- For rationale behind major hardware, module, or design changes, consult `DECISIONS.md`.
- If existing code and current documentation contradict each other, the current documentation takes precedence.
- Persistence is extended deliberately without creating existing configuration values a second time.
- Resume state and fault states are part of the mandatory migration scope.

## Mandatory Working Rules

### 1. Keep The Project Structure

- The project remains a classic Arduino sketch project.
- The production sketch is located at `sketches/Smaeenhouse/Smaeenhouse.ino`.
- Production header and CPP files remain in `sketches/Smaeenhouse/` unless explicitly requested otherwise.
- Hardware test sketches live under `hardware-tests/`.
- Do not reintroduce the old root-level `Smaeenhouse/` or `module-sketches/` folders as parallel runtime paths.

### 2. Never Overwrite Credentials

- `sketches/Smaeenhouse/Credentials.h` is local and secret.
- Never write real credentials to the repository.
- Only `sketches/Smaeenhouse/Credentials.example.h` may be changed as a template.
- Compile scripts may create a temporary `Credentials.h` from the example only when no real file exists, and must remove that temporary file afterwards.

### 3. Use The Repository Scripts

- Use `scripts/setup-arduino` only for explicit setup, because it may update indexes and install cores/libraries.
- Use `scripts/check-arduino` for normal compile checks.
- `scripts/check-arduino` must not install cores, install libraries, or update indexes.
- Use `scripts/cleanup-worktree` for generated worktree files.
- Do not use `scripts/cleanup-arduino-toolchain` automatically; it is manual project-end cleanup only.
- Do not commit `.build/`, `.arduino-cache/`, `.local/`, or `.arduino/`.

### 4. No I2C Logic In ISRs

- ISRs may only set flags or count pulses.
- No Wire/I2C communication in ISRs.
- No sensor reads in ISRs.
- This explicitly also applies to:
  - SHT alert
  - DS3231 SQW/INT
  - external AT24C32 EEPROM access
  - AD5263 access

### 5. No MQTT/HA Logic In ISRs

- MQTT, ArduinoHA, and publishes only in the main loop or normal methods.
- ISRs remain minimal.

### 6. Strictly Separate Light Logic

There are two control worlds:

- `light_auto_mode = ON` -> Arduino schedule active, HA schedule ignored
- `light_auto_mode = OFF` -> HA control active, Arduino schedule ignored

This separation must not be weakened.

### 7. HA Dimming Job Only Through The Defined Entities

Timed HA dimming jobs are modeled exclusively through these entities:

- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

Do not introduce alternative JSON/string commands unless the user explicitly requests them.

### 8. Persistence Through External RTC EEPROM

- Use the **AT24C32 EEPROM** of the WINGONEER Tiny DS3231 AT24C32 I2C module.
- Do not use `SAMD_SafeFlashStorage` persistence anymore.
- Use the **JC_EEPROM** library for the external EEPROM.
- Encapsulate EEPROM access in a project-internal persistence layer.
- Write only when values actually changed.
- Avoid unnecessarily frequent writes during normal runtime.
- Extend persistence deliberately with resume state without creating existing values a second time.

### 9. Use RTC Alarms For The Arduino-Internal Light Schedule

- Use both DS3231 alarms for the internal Arduino light schedule.
- `Alarm1` and `Alarm2` represent the stored on/off or dim-on/dim-off times.
- The times from persisted configuration are written to the DS3231 alarm registers after boot, time sync, and configuration changes.
- The SQW/INT output of the DS3231 is connected to `PIN_RTC_ALARM`.
- The ISR only sets a flag; evaluation happens in the main loop through RTClib.

### 10. Do Not Add Libraries Unnecessarily

Do not introduce new libraries if:

- existing project libraries are sufficient
- the function can be implemented with standard Arduino means

If a new library is still needed:

- document the reason in the change
- update `libraries.txt`
- update `sketches/Smaeenhouse/sketch.yaml`
- update setup scripts when the dependency must be installed by Arduino CLI

### 11. Republish Home Assistant States Cleanly

After startup or MQTT reconnect, these must be published again to HA:

- light state
- switch states
- number configuration values
- relevant sensor values

### 12. FanController Remains Dumb Regarding Thresholds

- FanController must not evaluate its own temperature/RH thresholds.
- The decision comes exclusively from the SHT alert logic.

### 13. Soil Moisture Calibration Remains HA-Controlled

- Do not invent an internal calibration routine in the firmware.
- Firmware provides raw values and stores final calibration data.
- HA guides the user through the routine.
- Do not introduce additional soil capture buttons such as `capture_soil_air` or `capture_soil_water` unless explicitly requested.

### 14. Align Changes With Documentation First

Before larger code changes:

- consider `SPEC.md`
- consider `MODULES.md`
- consider `HARDWARE.md`
- consider `docs/entity-model.md`
- check `ROADMAP.md` for open validation or follow-up items
- check `DECISIONS.md` for rationale behind major design choices
- consider the existing schematic in the project folder

### 15. Use SHTa Routines

- `SHTa.h/.cpp` should continue to be used.
- Ask before changing them and provide a reason.

### 16. Keep Documentation Understandable For New Users

- Hardware sections should be written so the project is understandable on first read.
- Mention the schematic in the project folder as the primary reference.
- Text documentation should also describe signal conditioning for:
  - light dimmer (AD5263 resistance path between `Dim+` and `Dim-`)
  - fan tachometer (2N3904 stage)
- Do not duplicate roadmap items into README/SPEC/MODULES unless they are actual current requirements.
- Keep open tasks in `ROADMAP.md` and historical rationale in `DECISIONS.md`.

If code and documentation contradict each other, documentation comes first, unless the user explicitly says otherwise.

### 17. Treat Real Measurement Data As Sensitive

- Real sensor histories, Home Assistant exports, timestamped measurement tables, calibration datasets, and analysis files derived from the real cabinet or room environment are sensitive project data.
- Do not commit or push real measurement data to GitHub.
- Keep local exports under ignored folders such as `measurements/`, `data/`, `exports/`, `ha-history/`, or `home-assistant-history/`.
- Documentation may include anonymized, synthetic, or heavily summarized examples when needed, but not raw real-world histories.

## Preferred Working Order

1. Fix compile errors first
2. Then clean up state logic
3. Then complete HA entity mapping
4. Then polish / logging / debugging

## Build / Verify

After code or build configuration changes, run:

```powershell
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions, run:

```bash
./scripts/check-arduino.sh
```

For documentation-only changes, diff review is sufficient.

## Not Wanted

- large logic blocks directly in `sketches/Smaeenhouse/Smaeenhouse.ino`
- hidden global side effects
- blocking `delay()` chains
- vendoring/checking in libraries without a reason
- spontaneous changes to the repository structure
