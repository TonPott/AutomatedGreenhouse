# Hardware Module Qualification Tests

Manual module qualification sketches belong here.

`hardware-tests/` is for validating whether an individual module, library, wiring concept, pin assignment, signal level, timing behavior, or sensor/actuator assumption is suitable for this project before production firmware relies on it.

These tests are separate from production firmware and from planned OTA-capable system or diagnostic tests under `system-tests/`.

Hardware module qualification tests normally run locally on real hardware. They can document expected compile behavior and intended Serial output, but they cannot be fully validated in CI because sensors, actuators, signal levels, and timing behavior depend on the physical build.

Detailed documentation for a specific test belongs in that test folder's own `README.md`. This root README provides global orientation, compile conventions, and cross-test rules only.

## Available Sketches

- `AD5263Test`
- `ClockServiceTest`
- `CQRTSL25911Test`
- `FanControllerTest`
- `HAInterfaceTest`
- `LightControllerTest`
- `MoistureSensorTest`
- `NetworkDiagnosticsTest`
- `NetworkManagerTest`
- `PersistentConfigTest`
- `ShtModuleTest`

Credential and network requirements must be documented in each test folder's README when they apply.

## Compile Checks

Run `scripts/setup-arduino` once per local machine before compile checks. Normal compile checks should not download, install, or update Arduino cores or libraries.

Default production compile:

- Windows: `.\scripts\check-arduino.ps1`
- Linux/macOS: `./scripts/check-arduino.sh`

The default production sketch is `sketches/Smaeenhouse`.

Hardware test compile:

- Windows PowerShell:
  - `$env:SKETCH = "hardware-tests/<test-folder>"`
  - `.\scripts\check-arduino.ps1`
- Linux/macOS: `SKETCH=hardware-tests/<test-folder> ./scripts/check-arduino.sh`

Hardware test folders normally do not contain their own `sketch.yaml`; therefore the scripts compile them with the `FQBN` fallback. If a future test needs its own profile, add a local `sketch.yaml` to that test folder deliberately.

## Test Folder Rules

- Keep tests small.
- Cover one hardware topic per test.
- Keep test-only code out of production firmware.
- Document detailed wiring, upload procedure, expected Serial output, and safety notes in the test folder's own README.
- Keep this root README focused on global conventions and index-style orientation.
- Never test mains wiring from a sketch without a safe physical setup.
- Copy findings that change requirements or validate assumptions back into `SPEC.md`, `HARDWARE.md`, `ROADMAP.md`, or `DECISIONS.md`.

## Relationship To System Tests

Use `system-tests/` for planned OTA-capable system or diagnostic tests that involve installed-system debugging, multi-module behavior, long-running diagnosis, or remote iteration.

Do not treat `hardware-tests/` as the place for OTA system-test workflows. Module qualification tests may be simpler and more manual than system tests, and Serial output remains the primary local diagnostic interface.
