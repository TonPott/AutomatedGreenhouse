# CQRTSL25911Test

Minimal bench test for the CQrobot CQRTSL25911 / TSL25911 cabinet light sensor.

## Purpose

This sketch validates the light sensor before production firmware depends on it.

It checks:

- I2C communication with the sensor at address `0x29`
- basic lux calculation through the Adafruit TSL2591 library
- raw full-spectrum, infrared, and derived visible-channel values
- the prepared INT line on Arduino D9 as a normal input

The sketch intentionally uses polling only. It does not attach an ISR and does not perform interrupt-driven I2C access.

## Wiring

- `VIN` / `VCC`: 3.3 V-compatible sensor supply according to the module wiring
- `GND`: common logic ground
- `SDA/SCL`: shared project I2C bus
- `INT`: Arduino D9 / future `PIN_LIGHT_SENSOR_INT`

The production schematic in `FullArduinoHouse.fzz` is the primary wiring reference.

## Running The Test

Compile:

```powershell
$env:SKETCH = "hardware-tests/CQRTSL25911Test"
.\scripts\check-arduino.ps1
```

Upload the sketch to the Arduino Nano 33 IoT and open Serial at `115200 baud`.

Expected startup output:

- test header
- expected I2C address `0x29`
- prepared INT pin `D9`
- `Sensor initialized.`

Expected periodic output:

- `Lux`
- `Full spectrum raw`
- `Infrared raw`
- `Visible raw`
- `INT pin D9`

## Captured Result

`OUTPUT.txt` contains one captured hardware run.

Observed results from that run:

- The sensor initialized successfully at `0x29`.
- 31 samples were captured.
- Lux ranged from `0.00 lx` to `2513.81 lx`.
- Average lux over the captured run was approximately `904.61 lx`.
- Full-spectrum raw values ranged from `1` to `1102`.
- Infrared raw values ranged from `1` to `278`.
- Derived visible raw values ranged from `0` to `824`.
- The prepared INT line on D9 stayed `HIGH` throughout the run.

Interpretation:

- Basic I2C communication and library-based lux conversion work.
- The sensor reacted clearly to dark, medium, and brighter light conditions.
- The D9 INT wire appears electrically idle/high in this polling-only test, which is acceptable for the current firmware because no interrupt behavior is used yet.
- The captured values are suitable as first plausibility data, but not as final cabinet calibration. Future validation should repeat measurements with known lamp dimming levels and final sensor placement.

## Notes

Keep future real measurement histories local unless a short validation capture is deliberately added as a project artifact. Do not commit Home Assistant exports, long timestamped histories, or derived private environment datasets.
