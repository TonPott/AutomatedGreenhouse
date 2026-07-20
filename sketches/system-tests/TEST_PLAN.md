# System Test Plan

This plan defines the ordered system-test path from the completed OTA smoke test toward full project-requirement validation. It is intentionally separate from individual test READMEs: this file describes the sequence and dependency structure, while each sketch folder documents its own behavior, safe states, observations, results, and notes for the next test.

## Planning Principles

- `sketches/system-tests/00_OtaSmokeTest` is the completed baseline and is not repeated here as open work.
- Create one new sketch at a time. Do not create the next sketch until the previous sketch's required functions have been confirmed on the installed system.
- Each new sketch must preserve the confirmed runtime behavior from the previous sketch unless a later README explicitly documents a deliberate replacement.
- After the OTA smoke, safe-output, I2C, and SHT hardware baselines, long-run system tests should publish a focused set of production-relevant Home Assistant entities through the `Grow Controller Tests` device so Home Assistant can retain useful history graphs during multi-day runs. Temporary diagnostics may still use direct MQTT test topics or Serial output instead of adding extra HA entities.
- Every sketch that touches actuators must put all connected peripherals into safe states during boot before network services start. Safe states include, at minimum: relay open, fan off, AD5263 shutdown or a documented safe dimmer setting, and no unintended I2C activity from ISRs. Outputs that are not part of the current test must not have entities, commands, callbacks, or local code paths that can change them after setup.
- Test READMEs must include a `Results And Notes For The Next Test` section. That section records confirmation status, anomalies, safe operating limits, and carry-forward assumptions that the next sketch must respect.
- Test sketches should publish production-equivalent HA entities when those values are useful for validating the feature under test or for long-run trend review. Remove obsolete retained discovery/state topics from earlier iterations when changing the HA entity set so Home Assistant does not show stale test entities. Other observations should remain under the separated `smaeenhouse/test/<test_id>/...` MQTT namespace.
- Real measurement histories, HA exports, and raw cabinet datasets are sensitive. Keep captures in ignored local paths and document only summarized or anonymized results.

## Required Per-Sketch README Additions

Each new sketch README must include the standard per-test documentation from `sketches/system-tests/README.md` plus this explicit section:

```text
## Results And Notes For The Next Test

- Confirmation status:
- Date / firmware revision:
- Required observations:
- Anomalies or limitations:
- Safety notes to carry forward:
- Entity or topic notes to carry forward:
```

If the test is not yet run, leave this section with `Not run yet` and the exact observations that must be filled in before the next sketch is created.

## Ordered Test Sequence

### 0. OTA Smoke Baseline - Complete

Existing sketch: `sketches/system-tests/00_OtaSmokeTest`

Confirmed scope:

- WiFi connection and indefinite reconnect attempts
- OTA availability independent of MQTT and Home Assistant
- regular OTA polling without long blocking delays
- MQTT/ArduinoHA uptime publication through the dedicated test device
- reboot and continued OTA availability after upload

Carry-forward rule: every later OTA-capable system test keeps OTA polling early and regular, keeps WiFi reconnect non-blocking, and must not make OTA depend on MQTT, Home Assistant, sensors, actuator jobs, or command loops. Because no standalone mode exists yet, stable WiFi recovery has priority over all later feature validation. Every sketch must retry indefinitely, reinitialize the NINA interface after repeated connection timeouts, expose recovery counters, and pass a controlled outage/recovery check before its feature result is accepted.

### 1. Safe Installed-System Baseline

Purpose: prove that a sketch can boot with the full installed peripheral set connected and immediately place all outputs in safe states before adding bus or actuator behavior.

Required behavior:

- Initialize all known output pins to safe states as early as possible.
- Keep the light relay open.
- Keep the fan switch off.
- Keep AD5263 `SHDN` asserted or otherwise document the verified safe dimmer state.
- Configure interrupt-capable input pins without doing I2C, MQTT, HA, or sensor reads in ISRs.
- Preserve OTA smoke-test network behavior and MQTT test status output.
- Publish no production HA entities unless a status entity is explicitly needed for the final firmware model; prefer direct MQTT test status.

Exit criteria before creating the next sketch:

- Safe states are observed at boot, during WiFi reconnect, during MQTT outage, and during OTA upload/reboot.
- No actuator moves unexpectedly with the complete installed wiring connected.
- The README records any inverted relay/fan behavior or pin-level surprise that later tests must account for.

### 2. I2C Inventory And Passive Sensor Baseline - Complete

Purpose: validate that all installed I2C devices can coexist on the bus while every actuator remains safe.

Required behavior:

- Preserve the safe-state baseline; any actuator output not explicitly under test is initialized to its safe state in setup and then left without any command or code path that can change it.
- Probe only documented project I2C addresses: SHT31, DS3231, AT24C32 at `0x57`, AD5263 at `0x2C`, and TSL25911 at `0x29`.
- Do not scan arbitrary addresses as production behavior; any broad diagnostic scan must be documented as test-only.
- Initialize passive sensors and publish or log temperature, humidity, RTC time/status, and light-sensor raw/lux values.
- Keep AD5263 and relay in safe states; do not energize the lamp.
- Publish production-relevant HA sensor/fault entities only for the sensors being validated, if HA is part of this test. Use direct MQTT test events for extra diagnostics.

Exit criteria:

- Expected devices respond consistently with the installed harness.
- Missing or unstable devices set or log the corresponding production fault concept: `sht_fault`, `rtc_fault`, `eeprom_fault`, `light_sensor_fault`, or `light_fault` for AD5263 reachability.
- No I2C access happens inside ISRs.

Confirmed result: accepted after the SHT address was corrected to the documented project address `0x45` and the follow-up SHT hardware baseline confirmed stable communication at that address. Carry forward that the original long I2C run exposed a WiFi-disconnect risk, while the later SHT run stayed online for more than 183,000 seconds with recovery counters published; a controlled outage/recovery test remains required before the network behavior is fully validated.

### 2a. SHT Hardware Baseline - Complete

Purpose: validate the installed SHT31 address, measurement path, stored alert-limit reads, and alert-pin monitoring before allowing SHT-driven fan behavior.

Confirmed scope:

- SHT31 responds at the fixed project address `0x45`; `0x44` does not respond on this hardware.
- Temperature and humidity measurements remained plausible and updated through a long run.
- Stored high/clear/low alert limits were readable and decoded.
- The alert ISR remained minimal and only recorded whether the interrupt was observed.
- Fan, relay, and AD5263 safe outputs remained unchanged.

Carry-forward rule: the next fan test must re-check the observed latched/status-register detail where the SHT alert summary bit was set while decoded RH/temperature alert bits were false and the alert line stayed high. Do not energize the fan automatically until thresholds are explicitly written or confirmed, alert-line behavior is understood, and tach feedback is validated.

### 3. Persistence And RTC Alarm Configuration Test

Purpose: validate external EEPROM persistence, RTC time handling, and DS3231 alarm register programming without actuating the light.

Required behavior:

- Preserve previous safe states and passive sensor behavior.
- Read and write only through the AT24C32 persistence layer pattern intended for production.
- Use a clearly documented test record layout and allow the test to overwrite that EEPROM area completely; there are no production-relevant EEPROM records that need preservation at this stage.
- Write only changed values and record write counts or change decisions in test MQTT logs and HA diagnostic entities.
- Validate representative persisted values: fan auto mode, light auto mode, fallback mode, light schedule minutes, default dim duration, SHT thresholds, and soil calibration values.
- Program DS3231 Alarm1 and Alarm2 from persisted light schedule values, then verify alarm-fired handling through a minimal ISR flag plus main-loop evaluation.
- Trigger or simulate schedule times without closing the relay or releasing an unsafe dimmer state.
- Publish a focused Home Assistant entity set through the `Grow Controller Tests` device for long-run history: EEPROM status, EEPROM write/skip counters, persisted sequence/checksum state, RTC time/lost-power status, Alarm1/Alarm2 configured/seen counters, network recovery counters, OTA gap count, and safe-output status. Keep extra low-level diagnostics on direct MQTT test topics.
- Remove obsolete retained HA discovery and state topics for entities that are renamed or dropped by this test before accepting the run.

Exit criteria:

- Values survive reboot and OTA update.
- Alarm flags are handled in the main loop, not in the ISR.
- Alarm updates occur after boot, time sync, and configuration changes.
- Home Assistant shows clean long-run histories for the selected `Grow Controller Tests` entities, with no stale retained entities from earlier test revisions.
- README notes identify EEPROM write frequency and any RTC/alarm edge cases for the next light tests.

### 4. SHT Alert And Fan Closed-Loop Test

Purpose: validate the required fan behavior driven exclusively by SHT alert logic.

Required behavior:

- Preserve OTA, safe-state, I2C, RTC, and persistence behavior.
- Configure SHT thresholds from persisted or HA-provided values and write them to the sensor at startup.
- Use the SHT alert ISR only to set a flag.
- Decode SHT alert status in the main loop and use it as the only automatic fan decision source.
- Support manual fan switch and fan auto-mode semantics through production-relevant HA entities.
- Measure fan RPM only when the fan should be running, using the documented 2-pulse-per-revolution tach behavior and inverted 2N3904-conditioned signal.
- Set `fan_fault` when the fan should run but tach pulses are absent after the documented grace period.

Exit criteria:

- Fan remains off in all idle and boot safe states.
- Manual and auto fan modes are separated correctly.
- FanController does not evaluate temperature or humidity thresholds internally.
- RPM and fault behavior are plausible on the installed fan.

### 5. Soil Moisture And Calibration Entity Test

Purpose: validate soil raw/percent publishing and HA-controlled calibration without adding a firmware calibration assistant.

Required behavior:

- Preserve all previous behavior.
- Read the SEN0308 analog value periodically and on `button.read_soil_raw_value`.
- Publish `soil_moisture_raw` and valid `soil_moisture_percent` according to `soil_air`, `soil_water`, and `soil_depth_mm`.
- Treat `soil_depth_mm < 20` as invalid for percent calculation while continuing raw publication.
- Expose only the production calibration entities: `soil_air`, `soil_water`, `soil_depth_mm`, and `read_soil_raw_value`.
- Do not add `capture_soil_air`, `capture_soil_water`, or an internal calibration state machine.

Exit criteria:

- HA can orchestrate air/water/depth calibration using only the documented entities.
- Persisted calibration values survive restart.
- Invalid-depth behavior does not publish misleading percent values.

### 6. AD5263 Dimmer Safe Readback Test

Purpose: validate AD5263 communication, mapping, shutdown behavior, and fault reporting before applying mains relay power.

Required behavior:

- Preserve previous behavior and keep the light relay open unless an explicitly documented manual safety step allows otherwise.
- Hold or enter AD5263 shutdown during boot until configuration and intended state are reconstructed.
- Write representative brightness targets to the AD5263 using the production mapping: 0%, 50%, 100%, and selected intermediate values.
- Verify readback or plausibility according to the production light fault strategy.
- Publish `light_fault` and `light_fault_reason` only with production-relevant reason values.
- Confirm that AD5263 errors keep or put the relay open.

Exit criteria:

- RDAC writes match expected mapping and readback behavior.
- `SHDN` sequencing is safe and reproducible.
- Any AD5263 failure mode leaves the lamp power relay open and records the correct fault reason.

### 7. Light Relay And Manual HA Control Test

Purpose: validate controlled light power sequencing and HA-mode manual light behavior after the dimmer path is proven safe.

Required behavior:

- Preserve previous behavior.
- Enforce startup sequence: keep AD5263 in shutdown, load/reconstruct state, set resistance, release shutdown, then close the relay only for a valid on command.
- Enforce shutdown sequence: open relay before putting AD5263 into shutdown.
- Validate `light_auto_mode = OFF` behavior for `light.grow_light`, brightness, and on/off control.
- Validate `light_hard_power_off` as an always-available safety override that opens the relay and retains internal dimmer state.
- Keep Arduino schedule events ignored while HA mode is active.

Exit criteria:

- Manual HA light control behaves like the target production light entity only in HA mode.
- Hard power off works in all light modes and never requires hiding the entity.
- No race allows relay closure before AD5263 is configured and released safely.

### 8. Arduino Schedule And RTC Alarm Light Test

Purpose: validate autonomous Arduino light schedule behavior through DS3231 alarms.

Required behavior:

- Preserve previous behavior.
- Validate `light_auto_mode = ON` with DS3231 Alarm1/Alarm2 as the schedule triggers.
- Use the RTC alarm ISR only to set a flag; evaluate alarm identity and actions in the main loop.
- Start scheduled on/off dimming requests from alarm events using `light_on_time_minutes`, `light_off_time_minutes`, and `light_dim_minutes`.
- Ignore HA schedule/manual on-off commands that are not allowed while Arduino auto mode is active, except for explicitly permitted temporary brightness correction if implemented.
- Terminate running dimming requests when switching between HA and Arduino control worlds.

Exit criteria:

- Exactly one light control source is active at a time.
- Arduino schedule events control the light only when `light_auto_mode = ON`.
- HA light commands and HA dim jobs are ignored or constrained as documented while auto mode is active.

### 9. HA Dimming Job And Resume-State Test

Purpose: validate HA timed dimming through the mandatory entities and restart resumption behavior.

Required behavior:

- Preserve previous behavior.
- Accept HA dimming requests only through `ha_dim_target_percent`, `ha_dim_duration_minutes`, and `start_ha_dim`.
- Start HA dim jobs only when `light_auto_mode = OFF`.
- Replace an active HA dim job with a new valid request from the active control source.
- Persist resume state using RTC/Epoch-based timing, not `millis()`.
- Reconstruct last effective brightness, hard-power-off state, and active HA dimming progress after restart or OTA update.
- Avoid unnecessary EEPROM writes while still preserving required resume information.

Exit criteria:

- Running dim jobs resume correctly after OTA/reboot.
- HA dim jobs are ignored in Arduino auto mode.
- No alternative JSON/string command path is introduced.

### 10. Network Outage, Republish, And Fallback Test

Purpose: validate production-like recovery after WiFi/MQTT/Home Assistant outages and the ten-minute light fallback rule.

Required behavior:

- Preserve previous behavior.
- Exercise WiFi loss, MQTT broker outage while WiFi remains connected, Home Assistant restart, and board OTA reboot.
- Keep local sensor, fan, RTC, EEPROM, and light schedule logic running during outages.
- After startup or MQTT reconnect, actively republish switch states, light state, number configuration values, fault states including `light_fault_reason`, and current sensor values.
- Validate `light_fallback_to_auto`: after connection loss exceeds ten minutes, either switch to Arduino auto mode or turn the light off according to the configured setting.
- Keep OTA available during all reconnect and fallback behavior.

Exit criteria:

- State republish is complete and does not require HA-side guesswork.
- Fallback behavior begins only after the documented timeout and remains safe.
- MQTT reconnect retry is periodic while WiFi is connected and does not depend only on a WiFi state transition.

### 11. Full Integrated Soak And Fault Injection Test

Purpose: validate the complete installed system over an extended period with representative commands, outages, and recoveries.

Required behavior:

- Preserve all confirmed behavior from earlier sketches.
- Run all production-relevant modules together with production-relevant HA entities.
- Capture direct MQTT test events to ignored JSONL files for later analysis.
- Inject or simulate representative faults where safe: missing I2C device, AD5263 write/readback failure, fan tach absence, SHT reset status, MQTT outage, WiFi outage, invalid soil depth, and RTC/NTP failure classes where practical.
- Keep actuator safety rules active during all injected faults.

Exit criteria:

- The complete entity model behaves consistently after boot, reconnect, OTA update, faults, and recovery.
- Mandatory fault entities are understandable in Home Assistant.
- README findings list remaining production-firmware gaps or explicitly state that requirements are validated by the system-test sequence.

## Completion Criteria For The System-Test Series

The series is complete when the final integrated test README summarizes confirmed coverage for these requirement groups:

- OTA, WiFi reconnect, MQTT reconnect, and republish behavior
- safe boot, safe outage, safe OTA, and safe reset states for all connected actuators
- I2C device coexistence without ISR bus access
- SHT alert thresholds and fan control responsibility separation
- fan tach/RPM and `fan_fault`
- soil raw/percent measurement and HA-controlled calibration
- AD5263 mapping, readback/fault handling, and `SHDN` sequencing
- light relay sequencing and hard-power-off override
- strict separation of HA light control and Arduino auto schedule control
- DS3231 alarm scheduling through Alarm1 and Alarm2
- HA dimming through only the defined number/button entities
- external AT24C32 persistence and RTC/Epoch-based resume state
- fault entities and `light_fault_reason`
- privacy-safe capture and documentation of results
