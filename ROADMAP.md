# Roadmap

This file tracks open work, next steps, validation needs, and optional improvements. It is intentionally separate from `README.md`, `SPEC.md`, `MODULES.md`, and `docs/entity-model.md`, which describe the current target system.

## Immediate Validation

- Run an Arduino IDE compile/build check for the main sketch.
- Verify the repository script compile path with `scripts/check-arduino`.
- Run or verify the AD5263 bench test sketch after the corrected mapping.
- Verify that the current code and documentation stay aligned before merging.

## Firmware Follow-Up

- Optionally validate with a direct MQTT command that `soil_depth_mm=0` produces the retained canonical `unavailable` state for `soil_moisture_percent` while raw values continue to update. The normal HA UI intentionally permits only `20..120 mm`; this optional negative-path check does not block later tests.
- Consider whether changing `soil_air`, `soil_water`, or `soil_depth_mm` from HA should trigger an immediate `sampleNow()` and state publish instead of waiting for the next interval or manual raw read.
- Validate the persistent Alarm1/Alarm2 target brightness values in the Arduino schedule system test and use its unrestricted `0..100 %` sweep to determine the real lamp's lower active and effective full-output bounds before adding production constraints.
- Verify resume-state behavior after restart for manual mode, HA dim jobs, and Arduino auto mode.

## Hardware Validation

### Nano 33 IoT Interrupt Pin Compatibility

The interrupt assignments were checked against the actual `g_APinDescription` initializers in the Arduino SAMD core 1.8.14 Nano 33 IoT variant, not only against the SAMD21 multiplexer table. All active test-local SHT ALERT definitions now use the `A7` candidate for repeatable bench validation; production wiring and authoritative production pin documentation remain unchanged until that validation passes. Recheck the mappings after any Arduino SAMD core update.

| Signal | Current pin and core mapping | Assessment and planned action |
| --- | --- | --- |
| SHT31 ALERT | Historical: `D7` / `PA06` / `EXTERNAL_INT_NONE`; rejected candidate: `A2` / `PA11` / `EXTERNAL_INT_NONE`; candidate: `A7` / `PB03` / `EXTINT3` | The SAMD21 can multiplex PA11 to EIC11, but the installed Nano 33 IoT core deliberately exposes A2 as `EXTERNAL_INT_NONE`, so standard `attachInterrupt()` silently rejects it. Corrective Tests 05 and 08 validate the actual core mapping, configure the SHT31 push-pull active-high output as `INPUT`, and register A7 with `attachInterrupt(digitalPinToInterrupt(A7), ..., RISING)`. Do not migrate production firmware or authoritative pin documentation until the A7 hardware run passes. |
| DS3231 SQW/INT | `D10` / `PA21` / `EXTINT5` | Compatible. Keep the assignment and continue validating both RTC alarms on the existing hardware. |
| Fan tachometer | `A1` / `PB02` / `EXTINT2` | Compatible. Stable RPM plus functional no-pulse fault and recovery behavior passed in Test 05. Keep the assignment; only the optional isolated tach-wire electrical-path check remains. Do not assign another active interrupt source to `EXTINT2`; `A0` shares that EIC channel in the core but is currently used only as an analog soil input. |
| TSL25911 INT | `D9` / `PA20` / `EXTINT4` | Compatible and distinct from the RTC and fan channels. Keep it reserved and bench-test the interrupt behavior before enabling it in production firmware. |

Before accepting the SHT ALERT pin migration, use Test 05 v1.1.4 to verify physical routing, 3.3 V push-pull active-high behavior, `irq_attached=true`, repeated rising-edge counts across separate alert assertions, and simultaneous RTC and fan-tach interrupts. Test 03 remains useful for SHT transaction stability but its older edge configuration is not physical-polarity acceptance evidence. Future TSL2591 interrupt coexistence still needs its own test. After successful hardware validation, update the production `Config.h`, schematic, `HARDWARE.md`, `SPEC.md`, and `MODULES.md` together. Avoid NINA/SPI pins as replacement candidates.

- Validate the unrestricted AD5263 mapping and effective lower/upper resistance limits with the real lamp driver. Sweep the complete `0..100 %` command range in both directions, record the first reliably illuminated value and the first effective full-output value, and check for material turn-on/turn-off hysteresis or delayed response. Do not introduce a provisional minimum clamp; convert the measured results into compile-time installation bounds only after validation.
- Validate SHDN behavior and relay sequencing.
- Run System Test 08b as a supervised relay-contact-bypass diagnostic: power the controller and AD5263 before connecting lamp mains, keep manual mains disconnection immediately available, confirm that D4 remains HIGH, and compare stability with the accepted Test 08 relay-contact run. A stable bypass result shifts investigation toward mains-contact switching, inrush, or lamp-driver disturbance; another failure shifts it toward relay-coil, I2C/dimmer, power-integrity, or firmware causes. This test does not replace the relay or establish a production OFF mechanism.
- Optionally isolate the conditioned fan-tach electrical path by disconnecting only the tach signal while the fan remains powered; functional command-on/no-pulses fault detection and recovery already passed when the complete fan connector was disconnected and restored.
- Validate CQRTSL25911 placement, I2C address `0x29`, INT wiring on `PIN_LIGHT_SENSOR_INT`, and useful lux/raw ranges with lamp off and at representative dim levels.
- Revalidate reopened System Tests 02 v1.1.1, 03 v1.2.1, and 04 v1.2.1 before transferring their I2C, SHT31, or EEPROM routines into Test 05 or later combined tests. Preserve the historical accepted results, but require the focused scan/round-trip/write/soak/OTA evidence documented in each test README.
- Validate production WiFi reconnect after sketch upload or WiFiNINA module state transitions.
- Validate production MQTT reconnect after broker outage while WiFi stays connected.
- Validate production NTP serial diagnostics for success and representative failure cases.

## System And Diagnostic Test Follow-Up

- Introduce OTA-capable system and diagnostic tests under `sketches/system-tests/` after the documentation structure has been reviewed.
- Start with a minimal OTA smoke test that validates only WiFi connection, OTA startup, regular OTA polling, fixed-IP or normal DNS-hostname upload, reboot, and continued OTA availability.
- Keep OTA-capable system tests online by design, with indefinite reconnect attempts and no intentional permanent offline mode.
- Prefer direct MQTT test topics over Home Assistant entities for remote system-test interaction.
- Keep Home Assistant out of the default system-test interface; add at most one explicit HA integration test unless a later decision changes that.
- Add structured output capture tooling for MQTT-capable system tests, ideally writing JSONL files under ignored local paths such as `.local/test-runs/`.
- Keep MQTT Explorer available for manual interaction, but do not rely on it as the only serious analysis capture path.
- Define delay rules for OTA-capable system tests: no blocking delays over 5 seconds, intentional blocking delays over 1 second announced on Serial, and long workflows normally implemented as state machines.
- Add a shared system-test runtime only after the OTA smoke test shape is reviewed.
- Consider shared test credentials across production firmware, hardware module qualification tests, and system tests without committing real credentials.
- Keep mDNS as a future compile-time option for OTA-capable system tests; the project default is fixed-IP or normal DNS-hostname upload without an mDNS dependency.

## Home Assistant Follow-Up

- Implement or update HA dashboard/scripts separately from this firmware branch.
- Add HA automations for the documented `button.read_soil_raw_value` calibration workflow.
- Confirm that fault entities are displayed clearly in HA.
- Ensure any future diagnostic HA device uses diagnostic-specific entity IDs and does not reuse production MQTT discovery unique IDs.
- Use HA history to design future lux-hour assistance from cabinet illuminance, the outside brightness sensor, grow-light brightness, and time of day.

## Optional Improvements

- Consider replacing the brief blocking UDP NTP wait with a non-blocking NTP state machine.
- Consider introducing a single machine-readable dependency source used by setup and check scripts. Keep `libraries.txt` as human-readable dependency documentation unless or until that source is introduced.
- Consider a dedicated validity indicator for soil percent if HA cannot show unavailable cleanly.
- Consider a compact diagnostic view for current AD5263 codes or derived dimmer resistance, if useful later.
- Consider production firmware OTA only after OTA-capable system tests and their safety/runtime patterns have been proven and explicitly documented.
- Consider a small decision-log review before major future refactors.

## Done / Recently Aligned

- Production WiFi reconnect now forces disconnect, waits a settle interval, and uses bounded timeout-based connect attempts.
- Production MQTT reconnect now retries periodically while WiFi is connected.
- Production NTP diagnostics now distinguish DNS, UDP setup/send, missing response, short response, invalid timestamp, and `WiFi.getTime()` fallback/check outcomes.
- Network diagnostics sketch added for WiFi, DNS, MQTT, Home Assistant discovery, and NTP checks.
- External NTP validation confirmed `pool.ntp.org` as the preferred configured NTP server.
- Documentation translated to English.
- AD5263 mapping aligned to tested lamp direction.
- Soil entity names aligned to `soil_moisture_percent` and `soil_moisture_raw`.
- Soil depth correction added to documentation and firmware.
- AD5263 test sketch aligned with the corrected mapping.
- Test 05 SHT high/low classification, manual/automatic fan control, stable RPM, and functional fan-fault recovery accepted on the installed system; the complete fan connector was used for fault injection and the deviation is documented.
- Test 06 soil-moisture calibration accepted on 2026-07-28: valid values at multiple depths, `0..100 %` output, immediate button sampling followed by the 10-second interval, EEPROM-backed calibration, and WiFi recovery with local functions continuing. The unexecuted `0 mm` direct-MQTT negative path is documented as non-blocking.
- Test 07 AD5263 safe-readback version `1.0.1` completed on 2026-07-29: complete indexed HA history, representative targets plus 66%, all controlled fault injections with recovery, OTA, WiFi outage/recovery, and the final unchanged soak passed.
- Test 08 completed on 2026-07-30. Version `1.0.0` accepted safe relay/`SHDN` ordering, auto-mode rejection, immediate hard-power-off preemption, RTC neutrality, WiFi recovery, OTA safe state, and the extended soak. Version `1.0.1` corrected ArduinoHA's brightness scale, and the focused HA-history run confirmed commands and exact readbacks above the former apparent 39% ceiling through `100 %`.
