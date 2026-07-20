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

## Test Sequence Plan

The ordered plan for building OTA-capable installed-system tests is maintained in [`TEST_PLAN.md`](TEST_PLAN.md). Follow that plan when adding sketches: create only the next test in the sequence after the previous test README records confirmed results and carry-forward notes.

## Network Recovery Rule

There is no standalone operating mode in the current project phase. Every OTA-capable system test must therefore maintain WiFi as its highest-priority runtime dependency, retry indefinitely after link loss, and reinitialize the NINA interface after repeated connection timeouts. Test-specific READMEs must record controlled outage recovery and long-run observations before the tested feature is considered confirmed.

## Current System Tests

* [`00_OtaSmokeTest`](00_OtaSmokeTest/) - completed OTA, WiFi, and MQTT uptime smoke baseline.
* [`01_SafeInstalledBaseline`](01_SafeInstalledBaseline/) - safe installed-system baseline for connected actuator outputs and direct MQTT test status.
* [`02_I2cPassiveBaseline`](02_I2cPassiveBaseline/) - known-address I2C inventory while preserving confirmed safe actuator states.
* [`03_ShtHardwareBaseline`](03_ShtHardwareBaseline/) - SHT measurements, stored alert limits, address checks, and alert interrupt monitoring.
* [`04_PersistenceRtcBaseline`](04_PersistenceRtcBaseline/) - AT24C32 persistence, DS3231 time/alarm handling, and HA long-run telemetry with safe actuator outputs.

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

Serial output remains required for local bench validation and basic diagnostics.

For remote interaction, early bring-up tests may use direct MQTT test topics only. Longer system tests that benefit from retained history should publish a focused set of production-relevant Home Assistant entities through the `Grow Controller Tests` device, while keeping extra diagnostics under direct MQTT test topics.

When a test changes its Home Assistant entity set, remove obsolete retained discovery and state topics from older test revisions so Home Assistant does not keep stale entities.

A system test must not require Home Assistant for OTA availability.

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
