# Hardware Tests

Manual hardware validation sketches belong here.

Hardware tests are separate from production firmware. They validate wiring, libraries, pin assignments, signal levels, timing behavior, and module assumptions before production modules rely on those assumptions.

Hardware tests normally run locally on real hardware. They can document expected compile behavior and intended Serial output, but they cannot be fully validated in CI because sensors, actuators, signal levels, and timing behavior depend on the physical build.

## Available Sketches

- `AD5263Test`
- `ClockServiceTest`
- `CQRTSL25911Test`
- `FanControllerTest`
- `HAInterfaceTest`
- `LightControllerTest`
- `MoistureSensorTest`
- `NetworkManagerTest`
- `PersistentConfigTest`
- `ShtModuleTest`

`ClockServiceTest`, `NetworkManagerTest`, and `HAInterfaceTest` use credential-dependent modules. Compile checks can temporarily use the production `Credentials.example.h`; uploads to real hardware need real local credentials copied into the test folder or otherwise provided.

### CQRTSL25911Test

Minimal bench test for the CQrobot CQRTSL25911 / TSL25911 light sensor.

Wiring assumptions:

- I2C address: `0x29`
- I2C bus: shared `SDA/SCL`
- prepared INT line: Arduino D9 / `PIN_LIGHT_SENSOR_INT`

Expected Serial output:

- sensor initialization result
- lux value
- full-spectrum raw value
- infrared raw value
- derived visible raw value
- current D9 INT pin state

The sketch polls the sensor every 2 seconds. It does not attach an ISR and does not use interrupt-driven I2C. Use it to compare dark, room ambient, and grow-light conditions. Keep real measurement logs local and do not commit Home Assistant exports or timestamped sensor histories.

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
