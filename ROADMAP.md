# Roadmap

This file tracks open work, next steps, validation needs, and optional improvements. It is intentionally separate from `README.md`, `SPEC.md`, `MODULES.md`, and `docs/entity-model.md`, which describe the current target system.

## Immediate Validation

- Run an Arduino IDE compile/build check for the main sketch.
- Verify the repository script compile path with `scripts/check-arduino`.
- Run or verify the AD5263 bench test sketch after the corrected mapping.
- Verify that the current code and documentation stay aligned before merging.

## Firmware Follow-Up

- Check whether invalid `soil_moisture_percent` can be represented as truly unavailable in ArduinoHA.
- Current fallback behavior: keep publishing raw values and avoid refreshing a misleading percent value if the percent calculation is invalid.
- Consider whether changing `soil_air`, `soil_water`, or `soil_depth_mm` from HA should trigger an immediate `sampleNow()` and state publish instead of waiting for the next interval or manual raw read.
- Verify AD5263 readback/fault behavior on real hardware.
- Verify resume-state behavior after restart for manual mode, HA dim jobs, and Arduino auto mode.

## Hardware Validation

- Validate AD5263 mapping and effective lower/upper resistance limits with the real lamp driver.
- Validate SHDN behavior and relay sequencing.
- Validate fan tach fault detection.
- Validate soil moisture depth correction with real sensor placement.
- Validate CQRTSL25911 placement, I2C address `0x29`, INT wiring on `PIN_LIGHT_SENSOR_INT`, and useful lux/raw ranges with lamp off and at representative dim levels.
- Validate production WiFi reconnect after sketch upload or WiFiNINA module state transitions.
- Validate production MQTT reconnect after broker outage while WiFi stays connected.
- Validate production NTP serial diagnostics for success and representative failure cases.

## System And Diagnostic Test Follow-Up

- Introduce OTA-capable system and diagnostic tests under `system-tests/` after the documentation structure has been reviewed.
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
