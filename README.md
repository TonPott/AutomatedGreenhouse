# Grow Controller (Arduino Nano 33 IoT)

This repository contains the firmware and project documentation for a grow controller based on an **Arduino Nano 33 IoT**.

## Goal

The controller manages and monitors:

- temperature and humidity via an **SHT3x**
- a **12V 3-pin fan** with tachometer evaluation
- a **dimmable grow light** via AD5263 dimmer + relay
- a **capacitive soil moisture sensor**
- a **DS3231 RTC with AT24C32 EEPROM**
- **Home Assistant** integration via **MQTT** using **ArduinoHA**

The system should work with Home Assistant and also continue operating locally and autonomously if the connection is lost.

## Hardware

- Arduino Nano 33 IoT
- Temperature and Humidity Sensor CQrobot CQRSHT31FA (Sensirion SHT31-DIS-F, I²C + alert pin)
- 3-pin 12V PC fan
- WINGONEER Tiny DS3231 AT24C32 I2C module
  - RTC: **DS3231**
  - external EEPROM: **AT24C32**
  - SQW/INT output for RTC alarms
- DFRobot Capacitive Soil Moisture Sensor SEN0308
- ViparSpectra P1000 Grow Light
  - 230 V supply switched via 3.3 V relay module
  - dimming via a variable resistance between `Dim+` and `Dim-`
- AD5263BRUZ50 (on TSSOP-24 adapter) as digitally controlled resistance
- Home Assistant with MQTT

## Project Overview For New Users

If you are seeing the project for the first time, the recommended reading order is:

1. Review the **schematic in the project folder**
2. Read `SPEC.md` – functional requirements
3. Read `MODULES.md` – technical structure
4. Read `docs/entity-model.md` – Home Assistant entities
5. Read `libraries.txt` – required libraries

The schematic in the project folder is the primary wiring reference.
This README adds short textual explanations to the schematic.

## Pin Assignment

- SHT Alert: `7`
- RTC SQW/INT: `10`
- Fan Switch: `2`
- Fan Tach: `A1`
- Light Power Relay: `3`
- Light Dim SHDN: `4`
- Light Dimmer (AD5263): `I²C` via `SDA/SCL`
- Soil Moisture: `A0`

## Important Hardware Notes

### 1. RTC + EEPROM

The RTC module provides two functions:

- **DS3231** as precise RTC
- **AT24C32** as external I²C EEPROM for persistent configuration

The firmware uses:

- RTClib for the DS3231
- **JC_EEPROM** for access to the AT24C32 EEPROM (requires **Streaming** https://github.com/janelia-arduino/Streaming)

The **SQW/INT output** of the DS3231 is also used for the Arduino-internal light schedule.

### 2. Light Dimmer (AD5263BRUZ50)

The old PWM/RC/PC817 dimmer path is no longer the target concept in this branch. Instead, the grow light is dimmed via an `AD5263BRUZ50` as a digitally adjustable resistance between `Dim+` and `Dim-`.

Hardware summary:

- `AD5263BRUZ50` on TSSOP-24 adapter
- two AD5263 channels in series for approximately `0..100 kΩ`
- low effective resistance = low brightness, high effective resistance = high brightness
- no galvanic isolation in the dimmer path
- hard switching of the 230 V supply remains the relay's responsibility

Pin summary:

- Light Power Relay: Pin `3`
- Light Dim `SHDN`: Pin `4` with external `10 kΩ` pull-down
- Light Dimmer: I²C via `SDA/SCL`, address `0x2C`

The exact AD5263 wiring, RDAC mapping, boot sequence, and fault/readback strategy are documented in `SPEC.md` and `MODULES.md`.

### 3. Fan Tach Signal Conditioning

The fan tach signal is converted to 3.3 V logic for the Nano 33 IoT through a **2N3904 transistor stage**:

- fan tach with 10 kΩ pull-up to 9 V
- tach through 47 kΩ to the base of a 2N3904
- 100 kΩ from base to GND
- emitter to GND
- collector to `A1`
- 10 kΩ pull-up from collector to 3.3 V

The resulting signal is inverted; the firmware accounts for this through the selected interrupt edge.

## Important Libraries

See `libraries.txt`.

## Important Documentation Files

- `SPEC.md` – functional requirements list
- `MODULES.md` – technical module specification
- `AGENTS.md` – working rules for Codex / AI agents
- `docs/entity-model.md` – Home Assistant entity model
- `Credentials.example.h` – template for local credentials

## Use With Arduino IDE

The project is intentionally structured as a classic Arduino sketch folder:

- `Smaeenhouse/Smaeenhouse.ino`
- all `.h/.cpp` files in the `Smaeenhouse/` folder
- no PlatformIO requirement

Additional test sketches are located under `module-sketches/`; they allow individual modules to be tested separately on the board.

## Credentials

Create a local `Credentials.h` file based on `Credentials.example.h`.

**Important:** `Credentials.h` must not be committed to the repository.

## Persistence

Persistent configuration is stored in the **AT24C32 EEPROM** of the RTC module.
Access is performed through the **JC_EEPROM** library.

In addition, a small `Light Resume State` is planned so the target state can be reconstructed consistently after restarts.

## Time Synchronization And Light Schedule

Time is synchronized via NTP:

- during boot
- at least once per day afterwards

After that, the **DS3231** serves as the local time base.

The Arduino-internal light schedule is mirrored into the two DS3231 alarm registers.
Alarm events are reported to the Arduino through the SQW/INT output.

## Soil Moisture Calibration

The firmware does not manage a calibration wizard and does not contain an internal calibration state machine. Home Assistant orchestrates the workflow.

The firmware only provides these interfaces:

- periodic reading of the soil moisture raw value
- periodic calculation of soil moisture percent
- `sensor.soil_moisture_raw`
- `sensor.soil_moisture_percent`
- `button.read_soil_raw_value`
- persistent configuration values `number.soil_air`, `number.soil_water`, and `number.soil_depth_mm`

For internal ADC raw values, the firmware may defensively use the 12-bit range `0..4095`. The persistent HA calibration values `soil_air` and `soil_water` remain separate in the expected project range `0..1000` with step `1`.

The intended workflow is:

1. For the air value, the sensor is dry in air. HA calls `button.read_soil_raw_value`, waits for the updated value of `sensor.soil_moisture_raw`, and writes it to `number.soil_air`.
2. For the water value, the sensor is in water at the reference depth of `120 mm`. HA calls `button.read_soil_raw_value` again, waits for `sensor.soil_moisture_raw`, and writes it to `number.soil_water`.
3. The actual insertion depth in the substrate is entered manually in `number.soil_depth_mm`.

Separate firmware buttons such as `capture_soil_air` or `capture_soil_water` should not be added.

`soil_depth_mm` is an active correction parameter. The firmware uses a linear depth correction with `SOIL_REFERENCE_DEPTH_MM = 120`:

```text
depth_factor = soil_depth_mm / SOIL_REFERENCE_DEPTH_MM
percent = (soil_air - raw) / ((soil_air - soil_water) * depth_factor) * 100
```

The air reference corresponds to `0 %`, and the water reference at `120 mm` corresponds to `100 %`. Valid results are constrained to `0..100 %`. If `soil_depth_mm < 20`, the percent value is invalid/unavailable; the raw value may continue to be published.

## Home Assistant

The MQTT/HA integration is based on:

- Arduino Home Assistant Integration by Dawid Chyrzynski
  https://github.com/dawidchyrzynski/arduino-home-assistant

Further details are documented in `docs/entity-model.md`.

## Open Issues

- The final firmware implementation still needs to be fully verified against this AD5263 specification.
- The real hardware build must be validated, especially mapping, limits, SHDN behavior, and fault response.
- HA scripts/automations for the documented soil moisture calibration workflow still need to be implemented in the Home Assistant setup.

## Roadmap

1. Verify AD5263 mapping and dimmer limits in the real hardware build
2. Complete firmware alignment for resume-state and fault strategy
3. Add HA automations for `button.read_soil_raw_value` and writing to `number.soil_air` / `number.soil_water`
4. Perform end-to-end tests for boot sequence, restart recovery, and fault paths
