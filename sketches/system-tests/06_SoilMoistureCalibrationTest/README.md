# Soil Moisture Calibration Test

Sketch: `06_SoilMoistureCalibrationTest.ino`

Version: `1.0.4`

## Status

Complete on the installed system as of 2026-07-28, with the documented invalid-depth test deviation below.

This maintenance revision also uses the shared retained-entity manifest for the `Grow Controller Tests` device. On connection it removes discovery for every known Test 02-09 entity not active in this sketch and clears orphaned states from the other known shared data prefix. Cleanup uses the existing ArduinoHA connection and publishes at most one retained deletion per loop pass.

## Purpose

This OTA-capable system test adds the production soil-moisture measurement and calibration behavior to the confirmed Test 05 runtime. It validates periodic and requested raw readings, the depth-corrected percentage calculation, Home Assistant calibration controls, invalid-result availability, EEPROM persistence, restart behavior, and reconnect republishing.

This is not an automated calibration assistant. Home Assistant or the user guides the dry/wet/depth procedure through the documented production entities.

## System-Test Lineage

The sketch inherits the network, OTA, EEPROM/RTC, SHT alert, fan-control, and safe-output runtime from `05_ShtAlertFanClosedLoopTest`. The soil calculation is a test-local snapshot of the current production `MoistureSensor`; no hardware-test sketch is used as a runtime source.

## Hardware Under Test

- Arduino Nano 33 IoT
- SEN0308 analog soil-moisture sensor on `A0`
- AT24C32 EEPROM at `0x57`
- DS3231 RTC and both alarm paths
- SHT31 and fan paths retained from Test 05
- Light relay and AD5263 shutdown outputs retained only for safe-state supervision

The project schematic is the primary wiring reference. This corrective revision uses SHT ALERT on `A7` (`PB03` / `EXTINT3`) and validates the active Nano 33 IoT core mapping before attaching the ISR. Production wiring documentation remains unchanged until the shared A7 bench validation passes.

## Safe Boot And Runtime Behavior

Before Serial, I2C, storage, sensors, or networking are initialized, the sketch:

- opens the light relay
- asserts AD5263 `SHDN`
- commands the fan off

The fan remains governed by the already confirmed Test 05 manual/automatic logic. Soil readings never actuate the fan or light. No I2C, MQTT, Home Assistant, or analog conversion runs in an ISR.

## Soil Measurement Behavior

The sensor is sampled once during setup, every 10 seconds in the normal loop, and immediately when `button.read_soil_raw_value` is pressed. A button read resets the periodic interval so it does not immediately produce a duplicate sample.

The test uses the production calculation:

```text
depth_factor = soil_depth_mm / 120
percent = (soil_air - raw) / ((soil_air - soil_water) * depth_factor) * 100
```

The final percentage is rounded to the nearest integer and constrained to `0..100 %`.

A percentage is invalid when:

- `soil_depth_mm < 20`
- `soil_air == soil_water`
- the derived denominator is effectively zero

When invalid, `sensor.soil_moisture_raw` continues to update, while `sensor.soil_moisture_percent` publishes Home Assistant's canonical retained state `unavailable` instead of refreshing a misleading number. This state-payload approach preserves the shared device availability used by every entity; ArduinoHA 2.1.0 suppresses per-entity availability topics when shared availability is enabled. When the configuration becomes valid again, the next numeric sample restores the percentage automatically.

## Persistence

The sketch continues the version-1 Test 04/Test 05 EEPROM record and its boot/sequence counters. It does not introduce a second record or change the record layout.

Only these existing fields are changed by Test 06:

- `soil_air`
- `soil_water`
- `soil_depth_mm`

A changed calibration value increments the record sequence, writes the complete record, reads it back, and verifies both bytes and checksum. Repeating the current value is counted as a skipped write. Periodic measurements and button reads never write EEPROM.

## Home Assistant Entities

The ArduinoHA entity capacity is `64`. Starting from the 43 Test 05 entities, Test 06 replaces `sensor.sketch_name` and `sensor.sketch_version` with one `sensor.sketch_identity` entity and adds six soil entities, for 48 total:

- `sensor.sketch_identity`
- `sensor.soil_moisture_raw`
- `sensor.soil_moisture_percent`
- `number.soil_air`
- `number.soil_water`
- `number.soil_depth_mm`
- `button.read_soil_raw_value`

`soil_air` and `soil_water` accept `0..1000` with step 1. The normal Home Assistant range for `soil_depth_mm` is `20..120 mm` with step 1. For the explicit invalid-depth acceptance test, a raw MQTT command of `0` may be sent to the already discovered `number.soil_depth_mm` command topic; the callback deliberately accepts `0..120`, and no separate test-only entity is added.

`sensor.sketch_identity` is the only Home Assistant sketch-identity entity and publishes `06_SoilMoistureCalibrationTest v1.0.4` once per MCU boot. A failed publication is retried; a successful value is not duplicated after a same-boot MQTT reconnect. On its first diagnostic MQTT connection, Test 06 removes retained discovery and state topics for the retired `sensor.sketch_name` and `sensor.sketch_version` entities. All other relevant states are republished after reconnect.

## Direct MQTT Diagnostics

Detailed diagnostics are published in normal loop context under:

```text
smaeenhouse/test/soil_moisture_calibration/status
smaeenhouse/test/soil_moisture_calibration/event
```

The status JSON includes raw value, percentage, validity, active air/water/depth values, periodic/button/command/invalid counters, and the inherited SHT, fan, network, RTC, and EEPROM diagnostics.

The event topic reports boot identity, calibration commands, requested raw reads, percentage-validity transitions, persistence failures, and the inherited state transitions. Raw Home Assistant exports and timestamped environment histories must remain outside the repository.

## Credentials And Compile Check

Copy `Credentials.example.h` to the ignored local `Credentials.h` for upload. Never commit real credentials.

Compile from the repository root:

```powershell
$env:SKETCH = "sketches/system-tests/06_SoilMoistureCalibrationTest"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=sketches/system-tests/06_SoilMoistureCalibrationTest ./scripts/check-arduino.sh
```

The compile script may create `Credentials.h` from the example only when none exists and must remove that temporary file afterward.

## Installed-System Acceptance Procedure

1. Compile the sketch. Confirm temporary credential cleanup and the static rule that ISRs only set flags or count pulses.
2. Install version `1.0.2` by OTA. Confirm fan off initially, relay open, `SHDN` asserted, valid EEPROM/RTC states, both RTC alarms, SHT/fan behavior carried forward, and the single correct combined sketch-identity entity.
3. Confirm the first raw and percent values appear, then observe at least three 10-second updates. Check that the direct status counters and HA histories advance without EEPROM writes.
4. Press `Read Soil Raw Value`. Confirm an immediate new raw sample and event, followed by the next periodic sample approximately 10 seconds later.
5. With a stable probe condition, compare the published percentage with the documented formula using the active air/water/depth values. Allow only the documented integer rounding and `0..100` clamp.
6. Set depth successively to `120`, `60`, and `20 mm` while keeping the probe stable. Confirm the direction and approximate scaling of the calculated percentage after each next sample.
7. Publish `0` to the existing `soil_depth_mm` command topic. Confirm the stored number reports `0`, raw readings continue, percent becomes unavailable, and an invalid diagnostic event appears. Restore a valid depth and confirm automatic recovery.
8. Perform the HA-guided physical calibration: record a representative air value, record a representative water/reference value without shorting or damaging the probe, set the actual insertion depth, and confirm useful percentage behavior. Do not add capture buttons or an internal calibration workflow.
9. Repeat one unchanged calibration command and one changed command. Confirm the unchanged command increases only the skipped-write counter, while the changed command increments EEPROM sequence/write count and survives OTA/reboot. Confirm button and periodic samples still cause no EEPROM writes.
10. Disable WiFi for at least three connection timeouts and one NINA reset, restore it, and confirm MCU uptime remains continuous, soil sampling continues, recovery counters increase, and every non-identity HA state is republished.
11. Restore safe calibration values and run at least two hours. Reject OTA poll gaps, continuous EEPROM writes, unavailable raw data, unexplained percentage-validity changes, RTC/EEPROM/SHT faults, or actuator deviations.

## Safety And Privacy

- Never energize the light as part of this test.
- Never obstruct the fan or use the soil sensor to drive an actuator.
- Do not immerse electronics or the sensor connector; follow the probe manufacturer's permitted wetting area.
- Avoid shorting the probe during wet-reference measurements.
- Do not commit HA exports, raw measurement histories, timestamped cabinet data, calibration datasets, or derived real-environment files.

## Known Limitations

- The accepted functional result used the historical D7 polling fallback. Version `1.0.3` changes only the active SHT ALERT test configuration to `A7`; that interrupt path still requires the shared bench validation.
- Percentage updates after calibration changes occur on the next periodic sample or explicit raw-read button press; immediate resampling on every number change remains a possible later refinement.
- The `0 mm` invalid-depth path was not exercised during acceptance because the normal HA UI deliberately limits the entity to the valid `20..120 mm` range. A later optional check may publish `0` directly to the discovered number command topic.
- No fixed expected raw or percentage range is invented before the installed probe is physically calibrated.

## Results And Notes For The Next Test

- Confirmation status: Complete; the invalid `0 mm` negative test is accepted as a non-blocking deviation.
- Date / firmware revision: Version `1.0.2`, accepted on 2026-07-28.
- Confirmed observations: All requirements carried forward from the earlier system tests remained satisfied. The soil sensor produced appropriate values at multiple insertion depths. Percentage output stayed within `0..100 %`. The raw-read button sampled immediately and the next periodic sample followed approximately 10 seconds later. Calibration completed successfully, including an EEPROM write. WiFi reconnected successfully while local functions continued running.
- Anomalies or limitations: The HA entity exposes only `20..120 mm`, so `soil_depth_mm=0` could not be selected through the normal UI and invalid-percent availability was not observed on hardware. The direct MQTT command path remains available for an optional later negative test. This does not block Test 07 because the valid measurement, depth correction, calibration, persistence, timing, and outage behavior passed. The repository compile check passed with Arduino SAMD 1.8.14 (85,972 bytes flash and 11,024 bytes RAM).
- Safety notes to carry forward: Relay remained open and AD5263 `SHDN` asserted throughout Test 06. Preserve those states until Test 07 deliberately exercises only the dimmer's low-voltage resistance/readback path; soil measurement must remain sensor-only.
- Entity or topic notes to carry forward: Preserve the existing HA device identifier, data prefix, single combined sketch-identity entity, and all 48 current entities. Keep detailed soil diagnostics on the direct `soil_moisture_calibration` status/event topics.