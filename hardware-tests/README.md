# Hardware Tests

Manual hardware validation sketches belong here.

Hardware tests are separate from production firmware. They validate wiring, libraries, pin assignments, signal levels, timing behavior, and module assumptions before production modules rely on those assumptions.

Hardware tests normally run locally on real hardware. They can document expected compile behavior and intended Serial output, but they cannot be fully validated in CI because sensors, actuators, signal levels, and timing behavior depend on the physical build.

## Available Sketches

- `AD5263Test`
- `ClockServiceTest`
- `FanControllerTest`
- `HAInterfaceTest`
- `LightControllerTest`
- `MoistureSensorTest`
- `NetworkManagerTest`
- `PersistentConfigTest`
- `ShtModuleTest`

`ClockServiceTest`, `NetworkManagerTest`, and `HAInterfaceTest` use credential-dependent modules. Compile checks can temporarily use the production `Credentials.example.h`; uploads to real hardware need real local credentials copied into the test folder or otherwise provided.

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
- Document wiring before uploading a test sketch.
- Document expected Serial output.
- Document safety notes before actuating hardware.
- Never test mains wiring from a sketch without a safe physical setup.
- Copy findings that change requirements or validate assumptions back into `SPEC.md`, `HARDWARE.md`, `ROADMAP.md`, or `DECISIONS.md`.
