# System and Diagnostic Tests

System tests are planned test sketches for installed-system debugging, multi-module behavior, long-running diagnosis, and later OTA-based development workflows.

They are intentionally separate from `sketches/hardware-tests/`.

## Relationship To `sketches/hardware-tests/`

`sketches/hardware-tests/` contains module qualification tests. Those tests answer questions such as:

* Can this module, library, wiring concept, or signal behavior be used for this project?
* What raw data does the module produce under known local test conditions?
* What practical limits, wiring assumptions, or library behaviors must the production firmware account for?

System tests answer different questions:

* How do multiple modules behave together in the installed system?
* How does a sensor-actuator pair behave under changing real-world conditions?
* How can an already-installed module be diagnosed when it does not behave as expected?
* How can long-running behavior be observed without repeatedly opening the enclosure or using USB?

System tests may therefore be more extensive than module qualification tests. They should still be understandable, safe, documented, and reviewable.

## Current Status

This directory is a planned test area. It may contain documentation before OTA-capable test code exists.

Production firmware must not be treated as OTA-capable merely because this directory documents OTA-capable tests. Production OTA remains a future goal until explicitly implemented and documented in the production firmware.

## Shared SHT Alert Pin

All active system-test configurations use SHT ALERT on `A7` (`PB03` / `EXTINT3`). Test 03 v1.2.0 and the corrective Tests 05 and 08 validate the actual Nano 33 IoT core descriptor, configure the SHT31 push-pull active-high signal as `INPUT`, and call `attachInterrupt(digitalPinToInterrupt(A7), ..., RISING)`. Test 02 only polls the line because the passive baseline must not create or clear interrupt causes. Historical D7 polling results remain historical evidence; production pin documentation is updated only after the shared A7 hardware acceptance passes.

## Test Sequence Plan

The ordered plan for building OTA-capable installed-system tests is maintained in [`TEST_PLAN.md`](TEST_PLAN.md). Follow that plan when adding sketches. A successor may be prepared early only when the user explicitly requests it; its README must state the unmet entry condition, and it must not be installed until the preceding test records the required confirmed result.

## Network Recovery Rule

Every OTA-capable system test keeps retrying WiFi indefinitely after link loss and reinitializes the NINA interface after repeated connection timeouts. A test may deliberately validate a local RTC/sensor control path while disconnected, but that local path must not stop network recovery or OTA polling after reconnection. Test-specific READMEs record controlled outage recovery and long-run observations before the tested feature is considered confirmed.

## Current System Tests

* [`00_OtaSmokeTest`](00_OtaSmokeTest/) - completed OTA, WiFi, and MQTT uptime smoke baseline.
* [`01_SafeInstalledBaseline`](01_SafeInstalledBaseline/) - safe installed-system baseline for connected actuator outputs and direct MQTT test status.
* [`02_I2cPassiveBaseline`](02_I2cPassiveBaseline/) - reopened v1.2.4 passive baseline for the replacement SHT31 at `0x44`, with the standard ArduinoOTA storage path, real bus-level preflight, and non-stale bus-stuck diagnostics.
* [`03_ShtHardwareBaseline`](03_ShtHardwareBaseline/) - reopened v1.2.1 SHT periodic/idle transaction-state baseline with active-high A7 validation.
* [`04_PersistenceRtcBaseline`](04_PersistenceRtcBaseline/) - historical persistence/RTC results remain accepted; v1.2.1 is reopened for separated EEPROM transport, record, and recovery validation.
* [`05_ShtAlertFanClosedLoopTest`](05_ShtAlertFanClosedLoopTest/) - corrective v1.1.5 implemented and reopened for focused SHT, EEPROM, active-high A7, soak, and OTA acceptance; historically accepted fan-control steps remain valid.
* [`06_SoilMoistureCalibrationTest`](06_SoilMoistureCalibrationTest/) - completed soil raw/percent measurement, HA-controlled calibration, depth behavior, persistence, and reconnect validation; invalid `0 mm` remains an optional direct-MQTT negative test.
* [`07_Ad5263DimmerSafeReadbackTest`](07_Ad5263DimmerSafeReadbackTest/) - completed AD5263/readback, injection/recovery, OTA, reconnect, and final-soak validation with the relay held open.
* [`08_LightRelayManualHaControlTest`](08_LightRelayManualHaControlTest/) - light behavior complete; corrective v1.0.3 infrastructure revision reopened for focused SHT, EEPROM, A7, availability, and OTA confirmation.
* [`08a_LocalLightScheduleRuntimeTest`](08a_LocalLightScheduleRuntimeTest/) - separate-device interim test for a persistent local RTC schedule, non-blocking NTP/DST handling, minute raw sensors, and network-independent light control.
* [`08b_RelayBypassDimmerDiagnosticTest`](08b_RelayBypassDimmerDiagnosticTest/) - supervised separate-device diagnostic with bypassed relay contacts, continuously released SHDN, verified minimum-resistance dim-off, and retained Test 08 runtime coverage.
* [`09_ArduinoScheduleRtcLightTest`](09_ArduinoScheduleRtcLightTest/) - ready for unrestricted real-lamp boundary characterization and persistent Alarm1/Alarm2 schedule-target validation.

## Required Per-Test Documentation

Every system test must have its own `README.md` in the test folder.

The root `sketches/system-tests/README.md` describes global conventions only. Detailed behavior of a specific test belongs in that test's own README.

Each test README should document:

* purpose
* hardware under test
* installed-system assumptions
* wiring assumptions
* credentials or network requirements
* upload procedure
* interaction method
* Serial output
* MQTT topics, if any
* commands, if any
* expected observations
* safety notes
* output capture procedure
* known limitations
* findings and interpretation notes

## Interaction Model

Serial output remains useful for local bench validation and basic diagnostics, but installed-system acceptance must not require a direct USB connection. For installed tests, every required observation must be available remotely.

When a test needs a time series or a multi-step result, Home Assistant is the authoritative collection path. Publish every step immediately through dedicated history entities with an explicit sequence/index value; do not rely on a periodic summary interval or MQTT Explorer to reconstruct intermediate states.

For remote interaction, early bring-up tests may use direct MQTT test topics only. Longer system tests publish their required history through focused Home Assistant entities on the `Grow Controller Tests` device. Do not maintain a parallel diagnostic MQTT client or redundant status/event topics when the same evidence is available through Home Assistant.

Every Home Assistant system test must remove known obsolete retained discovery and state topics. Tests 02-09 intentionally reuse the `Grow Controller Tests` device and therefore use the identical sketch-local `SystemTestHaCleanup.h` manifest: entries not active for the running test lose their discovery configuration, and orphaned states are cleared from the other known shared data prefix. The cursor publishes at most one retained deletion per loop pass through the existing ArduinoHA connection. Arduino builds copy only the selected sketch directory, so the manifest is mirrored in each affected directory; all copies and test masks must be updated together whenever an entity is added, removed, or renamed. Tests with their own device ID maintain an equivalent test-local retired-topic list. Never delete discovery belonging to another device ID.

The cleanup requirement applies on every test revision, not only when the current change happens to rename an entity.

A system test must not require Home Assistant for OTA availability.

## Boot Identity Publication

Every future system-test sketch must define a stable sketch name and an explicit sketch version. It exposes both together through exactly one shared Home Assistant test-device entity:

* `sensor.sketch_identity` with value `<sketch name> v<version>`

The default is one publication per MCU boot after Home Assistant MQTT first becomes available. A failed publication is retried, while an MQTT reconnect during the same MCU boot normally does not duplicate a successful identity publication. A test-specific README may require republishing the same combined identity on each HA connection when reconnect behavior itself is under acceptance, as in Test 08a. Separate `sensor.sketch_name` and `sensor.sketch_version` entities must not be created by future tests; a successor that replaces them must clear their retained discovery and state topics.

Do not mirror this identity to a redundant direct `boot_identity` MQTT topic. All identity publication happens in normal loop context, never in an ISR.
## Suggested MQTT Topic Convention

Future MQTT-capable system tests should use a clearly separated test namespace, for example:

```text
smaeenhouse/test/<test_id>/status
smaeenhouse/test/<test_id>/log
smaeenhouse/test/<test_id>/event
smaeenhouse/test/<test_id>/cmd
smaeenhouse/test/<test_id>/result
```

Suggested topic meanings:

* `status`: retained or periodically refreshed high-level test state
* `log`: human-readable diagnostic messages
* `event`: structured machine-readable events
* `cmd`: incoming test commands
* `result`: final or intermediate result summaries

Payloads intended for analysis should be JSON where practical.

## Output Capture

MQTT Explorer is suitable for manual inspection and command publishing, but it should not be the only capture path for serious analysis.

Future tooling should include a capture script that subscribes to test topics and writes JSONL files under a local ignored directory such as:

```text
.local/test-runs/<timestamp>-<test_id>.jsonl
```

Raw test logs should normally not be committed. Summarized, anonymized, or interpreted findings may be copied into the test README, `DECISIONS.md`, `ROADMAP.md`, or other appropriate documentation.

## OTA Principles For Future System Tests

OTA support is planned for system and diagnostic tests first. Production firmware OTA is a later goal.

For OTA-capable system tests:

* OTA polling is a low-level runtime responsibility.
* OTA must not depend on MQTT.
* OTA must not depend on Home Assistant.
* OTA must not depend on a test command loop, sensor read, actuator job, or long-running measurement.
* OTA polling should happen early and regularly in the main loop.
* Long-running behavior should be implemented as a state machine.
* Tests should keep trying to stay online and must not intentionally enter a permanent offline mode.

The project default should be fixed-IP or normal DNS-hostname upload. mDNS should be a compile-time option for users whose networks support it reliably.

## Delay Rules

For OTA-capable system tests:

* Blocking delays over 5 seconds are not allowed.
* Intentional blocking delays over 1 second must be announced on Serial before they start.
* Long-running actions should normally be implemented as non-blocking state machines.
* OTA polling must continue during long-running tests.

Module qualification tests in `sketches/hardware-tests/` may be more permissive, but long waits should still be documented and announced where practical.

## Safety Rules

Any test that controls actuators must document a safe state.

For OTA-capable tests, the future runtime should provide hooks that allow a test to enter a safe state when an OTA upload starts or before the board resets after an update.

Examples of safe actions may include:

* stopping an active test job
* switching a relay off
* setting a dimmer to a safe value
* disabling manual actuator commands
* publishing a final diagnostic event

Exact behavior must be documented by each test.

## Future Shared Runtime

A future shared runtime layer may be added for OTA-capable system tests, for example:

```text
sketches/system-tests/common/TestRuntime.h
sketches/system-tests/common/TestRuntime.cpp
```

Expected responsibilities:

* WiFi connection management for tests
* indefinite reconnect attempts
* OTA startup and polling
* optional mDNS configuration
* fixed-IP or hostname upload support
* basic Serial status output
* optional MQTT test status output
* non-blocking update cadence
* safe-state callbacks for OTA start/reset

The shared runtime should be introduced only after the test architecture is documented and a minimal OTA smoke test has been reviewed.

## Future Shared Credentials

A future shared credentials layout may be useful for production firmware, hardware tests, and system tests.

This is not implemented yet.

The expected direction is a common local credentials file that can be reused by multiple sketches without duplicating sensitive values in each test folder.

Any shared credentials design must preserve the current rule that real credentials are local only and must not be committed.
