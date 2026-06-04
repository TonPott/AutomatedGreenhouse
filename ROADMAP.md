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

## Home Assistant Follow-Up

- Implement or update HA dashboard/scripts separately from this firmware branch.
- Add HA automations for the documented `button.read_soil_raw_value` calibration workflow.
- Confirm that fault entities are displayed clearly in HA.
- Ensure any future diagnostic HA device uses diagnostic-specific entity IDs and does not reuse production MQTT discovery unique IDs.
- Use HA history to design future lux-hour assistance from cabinet illuminance, the outside brightness sensor, grow-light brightness, and time of day.

## Optional Improvements

- Consider a dedicated validity indicator for soil percent if HA cannot show unavailable cleanly.
- Consider a compact diagnostic view for current AD5263 codes or derived dimmer resistance, if useful later.
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
