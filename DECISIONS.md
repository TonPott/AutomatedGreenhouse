# Design Decisions

This file records important design, hardware, and architecture decisions for the project.
It explains why the current design looks the way it does.
It does not replace `README.md`, `SPEC.md`, `MODULES.md`, or `ROADMAP.md`.

## 2026-05 – Cabinet light sensor starts as measurement-only

Status: accepted

### Context

- The CQRTSL25911 sensor can only be placed inside the plant cabinet.
- When the grow light is on, the sensor measures the combined cabinet light environment and cannot independently evaluate room ambient light.
- A future light-sum design should also consider the outside brightness sensor that already exists in Home Assistant.

### Decision

- Integrate the CQRTSL25911 / TSL25911 first as a measurement-only firmware module.
- Publish cabinet illuminance and raw light channels to Home Assistant.
- Keep the existing Arduino schedule, HA dimming interface, and fallback behavior unchanged.
- Prepare the sensor INT line physically, but use polling in the initial firmware.
- Treat real measurement histories and HA exports as sensitive local data that must not be committed.

### Consequences

- The sensor can be logged in HA before any automatic control logic is designed.
- Future lux-hour compensation should be HA-first and should use existing HA dimming entities while `light_auto_mode = OFF`.
- Standalone/fallback lux-based behavior remains future work and must be designed explicitly before implementation.

### Affected Areas

- `SPEC.md`
- `MODULES.md`
- `HARDWARE.md`
- `docs/entity-model.md`
- `sketches/Smaeenhouse/LightSensor.*`
- `sketches/Smaeenhouse/HAInterface.*`

## 2026-05 – AD5263 replaces the old PWM/RC/PC817 dimmer concept

Status: accepted

### Context

- The old PWM/RC/PC817 dimmer path is no longer the target design.
- The lamp is controlled through a resistance between `Dim+` and `Dim-`.
- A digital potentiometer approach better matches the required control interface.

### Decision

- Use `AD5263BRUZ50` as the target dimmer component.
- Use two channels in series.
- Keep the hard relay path separate for 230 V power switching.
- Use SHDN for safe startup/shutdown behavior.

### Consequences

- The firmware uses AD5263/I²C control instead of PWM dimming.
- The old PWM/RC/PC817 path must not be reintroduced as a runtime option.
- Fault handling must protect against AD5263 communication/readback failures.
- Documentation and code must remain aligned on the AD5263 mapping.

### Affected Areas

- `SPEC.md`
- `MODULES.md`
- `sketches/Smaeenhouse/Config.h`
- `sketches/Smaeenhouse/LightController.*`
- `hardware-tests/AD5263Test/AD5263Test.ino`

## 2026-05 – Lamp dimmer direction is low resistance = low brightness, high resistance = high brightness

Status: accepted

### Context

- Real hardware testing showed that the previous mapping direction was inverted for the lamp.
- Swapping `Dim+` and `Dim-` is not expected to change a pure resistance input.

### Decision

- Treat minimal effective resistance / approximately `0 Ω` as `0 %`.
- Treat maximal effective resistance / approximately `100 kΩ` as `100 %`.
- Use the mapping:
  - `0 %  => W2 = 0,   W1 = 255`
  - `50 % => W2 = 0,   W1 = 0`
  - `100 % => W2 = 255, W1 = 0`

### Consequences

- Code, documentation, and AD5263 test sketch must use this mapping.
- Future changes to dimmer limits must preserve the direction unless new hardware evidence says otherwise.

### Affected Areas

- `SPEC.md`
- `MODULES.md`
- `sketches/Smaeenhouse/Config.h`
- `sketches/Smaeenhouse/LightController.cpp`
- `hardware-tests/AD5263Test/AD5263Test.ino`

## 2026-05 – Project documentation is maintained in English

Status: accepted

### Context

- The project should be useful to more readers.
- Chats with the user may continue in German, but repository documentation should be accessible internationally.

### Decision

- Maintain repository documentation in English.
- Code, identifiers, comments, and commit summaries remain English.

### Consequences

- `AGENTS.md` must tell Codex to keep documentation and code in English.
- German chat context should be translated into clean English repository documentation when applied.

### Affected Areas

- all documentation files
- `AGENTS.md`

## 2026-05 – Main documentation describes the current target state, not development history

Status: accepted

### Context

- README, SPEC, MODULES, and entity model are easier to use if they describe the current target project.
- Open tasks and historical rationale made the main docs less clear.

### Decision

- Keep README/SPEC/MODULES/entity-model focused on the current target state.
- Move open tasks to `ROADMAP.md`.
- Move rationale and historical design decisions to `DECISIONS.md`.

### Consequences

- README should not contain a long roadmap/open-issues section.
- SPEC and MODULES should not include version history or discarded implementation paths unless needed to describe the current state.
- AGENTS may reference ROADMAP and DECISIONS but should not duplicate them heavily.

### Affected Areas

- `README.md`
- `SPEC.md`
- `MODULES.md`
- `docs/entity-model.md`
- `AGENTS.md`
- `ROADMAP.md`
- `DECISIONS.md`

## 2026-06 – Network diagnostics stay separate from production firmware

Status: accepted

### Context

- Real hardware testing showed that network failures can look like firmware faults when WiFi, DNS, MQTT, Home Assistant discovery, and NTP are tested only through the production sketch.
- After uploading a new sketch while an older network test is running, the Nano 33 IoT / WiFiNINA stack may keep an old connection state long enough for the first WiFi connection attempt to fail.
- Home Assistant MQTT discovery uses entity unique IDs for registry identity, so diagnostic entities must not reuse production unique IDs.
- `WiFi.getTime()` can be used as an independent WiFiNINA module time check alongside explicit UDP NTP requests.

### Decision

- Keep the network diagnostics sketch as a standalone hardware test, not as production runtime logic.
- Use test-local placeholder credentials and do not include production `Credentials.h`.
- Use diagnostic-specific device and entity identifiers.
- Prefer `pool.ntp.org` as the default external NTP server for firmware and diagnostics.
- Keep diagnostic HA entities out of production firmware.
- Implement production WiFi reconnect hardening, periodic MQTT reconnect, and richer NTP error reporting without changing production HA entity IDs.

### Consequences

- Network troubleshooting can separate WiFi/DNS/MQTT/HA/NTP behavior from sensors, actuators, RTC, EEPROM, and dimmer hardware.
- Production firmware remains focused, but now carries the connection hardening learned from the diagnostics sketch.
- Future HA diagnostic devices should avoid unique ID collisions with production entities.
- NTP troubleshooting can compare explicit UDP requests with the WiFiNINA module time source, and production serial logs now report the relevant failure class.
- Production UDP NTP sync may still block briefly while waiting for a response. This is accepted for now because sync attempts happen only at boot, manual sync, or daily resync; a non-blocking NTP state machine remains a future improvement.

### Affected Areas

- `hardware-tests/NetworkDiagnosticsTest/NetworkDiagnosticsTest.ino`
- `hardware-tests/NetworkDiagnosticsTest/README.md`

## 2026-06 – Dependency declarations remain duplicated for now

Status: accepted

### Context

- Arduino dependencies are currently listed in multiple places: `libraries.txt`, `sketches/Smaeenhouse/sketch.yaml`, setup scripts, and check scripts.
- The CQRTSL25911 / TSL25911 integration added Adafruit sensor dependencies and exposed the risk that setup and check script lists can drift apart.

### Decision

- Keep the current duplicated dependency declarations for this branch.
- Keep `libraries.txt` as human-readable dependency documentation.
- Update setup/check script library checks explicitly when dependencies change.

### Consequences

- Dependency handling remains simple and compatible with the current scripts.
- Future work should consider a single machine-readable dependency source that setup/check scripts can consume.

### Affected Areas

- `libraries.txt`
- `sketches/Smaeenhouse/sketch.yaml`
- `scripts/setup-arduino.ps1`
- `scripts/setup-arduino.sh`
- `scripts/check-arduino.ps1`
- `scripts/check-arduino.sh`
- `ROADMAP.md`

## 2026-06 – Module qualification tests and OTA-capable system tests are separate

Status: accepted

### Context

- Existing sketches under `hardware-tests/` validate individual modules, libraries, wiring concepts, signal behavior, and practical hardware assumptions.
- Planned OTA-capable tests need a different shape because they support installed-system diagnostics, multi-module behavior, longer-running observations, and remote iteration.
- Home Assistant discovery and entities are useful for production integration, but they are not the best default interface for focused system-test control and capture.
- mDNS can be unreliable on some networks, especially across VLAN boundaries, while fixed IP addresses or normal DNS hostnames can still provide a stable OTA upload target.

### Decision

- Keep module qualification tests under `hardware-tests/`.
- Plan OTA-capable system and diagnostic tests separately under `system-tests/`.
- Do not make Home Assistant the standard system-test interface.
- Prefer Serial for required local diagnostics and direct MQTT test topics for optional remote interaction.
- Use fixed-IP or normal DNS-hostname upload as the default OTA test workflow.
- Keep mDNS as an optional compile-time feature for later tests, not as a default project dependency.
- Treat production firmware OTA as a later end goal, not as current implemented behavior.

### Consequences

- Root test READMEs stay focused on global conventions, index-style orientation, and cross-test rules.
- Detailed behavior for each test belongs in that test folder's own README.
- OTA-capable system tests must keep trying to stay online and must not intentionally enter a permanent offline mode.
- Long-running OTA-capable tests need non-blocking structure, normally state machines, so OTA polling remains available.
- The first OTA work should validate the smoke-test workflow before introducing shared runtime code, shared credentials, or production firmware OTA.

### Affected Areas

- `hardware-tests/README.md`
- `system-tests/README.md`
- `system-tests/OtaSmokeTest/README.md`
- `ROADMAP.md`
- `AGENTS.md`

## 2026-05 – Soil moisture calibration is HA-guided and firmware remains stateless for calibration workflow

Status: accepted

### Context

- The firmware provides raw and calculated values.
- HA orchestrates the calibration workflow.

### Decision

- Do not add firmware-side calibration state machines.
- Do not add separate capture buttons such as `capture_soil_air` or `capture_soil_water`.
- Use `button.read_soil_raw_value` and HA scripts/automations to write final values.

### Consequences

- Firmware remains simpler.
- HA handles user workflow.
- Documentation must keep `soil_moisture_raw`, `soil_moisture_percent`, `soil_air`, `soil_water`, and `soil_depth_mm` aligned.

### Affected Areas

- `SPEC.md`
- `MODULES.md`
- `docs/entity-model.md`
- `sketches/Smaeenhouse/MoistureSensor.*`
- `sketches/Smaeenhouse/HAInterface.*`

## 2026-05 – Soil depth correction uses a simple linear model

Status: accepted

### Context

- The sensor response depends on insertion depth.
- A measurement series exists, and the project accepts the linear approximation as sufficient.

### Decision

- Use:
  - `depth_factor = soil_depth_mm / SOIL_REFERENCE_DEPTH_MM`
  - `percent = (soil_air - raw) / ((soil_air - soil_water) * depth_factor) * 100`
- Treat values below `SOIL_MIN_VALID_DEPTH_MM = 20` as invalid/unavailable for percent calculation.

### Consequences

- Raw value may still be published.
- Percent value should not be refreshed with misleading data when invalid.
- Future calibration improvements should be documented before changing the model.

### Affected Areas

- `SPEC.md`
- `MODULES.md`
- `docs/entity-model.md`
- `sketches/Smaeenhouse/Config.h`
- `sketches/Smaeenhouse/MoistureSensor.*`
- `sketches/Smaeenhouse/HAInterface.*`
