## Module Test Sketches

These sketches are small Arduino-IDE-compatible harnesses for testing one module at a time.

Each sketch keeps the production code as the single source of truth by including the shared
implementation files from `Smaeenhouse/`.

Available sketches:

- `PersistentConfigTest`
- `ShtModuleTest`
- `FanControllerTest`
- `LightControllerTest`
- `MoistureSensorTest`
- `ClockServiceTest`
- `NetworkManagerTest`
- `HAInterfaceTest`

Notes:

- Open one sketch folder at a time in the Arduino IDE.
- `ClockServiceTest`, `NetworkManagerTest`, and `HAInterfaceTest` need a valid
  `Smaeenhouse/Credentials.h`.
- `HAInterfaceTest` is the only sketch that intentionally exercises multiple production modules,
  because `HAInterface` depends on them by design.
