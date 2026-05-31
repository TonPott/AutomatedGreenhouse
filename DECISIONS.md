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
