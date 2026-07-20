# HARDWARE.md - Physical Build And Wiring Overview

## Purpose

Document the physical hardware target for this firmware.

Keep wiring, pin assignments, power domains, and signal-level assumptions here so firmware changes can be reviewed against the real build. The schematic in the project folder (`FullArduinoHouse.fzz`) is the primary wiring reference.

## Target Board

- Board: Arduino Nano 33 IoT
- FQBN: `arduino:samd:nano_33_iot`
- Logic voltage: 3.3 V
- Firmware sketch: `sketches/alpha/Smaeenhouse/Smaeenhouse.ino`

## Connected Hardware

- CQrobot CQRSHT31FA temperature and humidity sensor (Sensirion SHT31-DIS-F, I2C + alert pin)
- 3-pin 12 V PC fan
- WINGONEER Tiny DS3231 AT24C32 I2C module
  - DS3231 RTC
  - AT24C32 external EEPROM
  - SQW/INT output for RTC alarms
- DFRobot Capacitive Soil Moisture Sensor SEN0308
- ViparSpectra P1000 grow light
  - 230 V supply switched via 3.3 V relay module
  - dimming through variable resistance between `Dim+` and `Dim-`
- AD5263BRUZ50 on TSSOP-24 adapter as digitally controlled resistance
- CQrobot CQRTSL25911 illuminance sensor (TSL25911, I2C + prepared interrupt line)
- Home Assistant with MQTT

## Power Domains

- USB / logic power: Arduino Nano 33 IoT 3.3 V logic domain.
- External actuator power: 12 V fan supply and relay-switched grow-light mains supply.
- Common ground requirements: low-voltage control/sensor circuitry must share the required reference ground according to the schematic.
- Mains wiring is outside firmware safety guarantees and must be built, isolated, fused, and enclosed safely.

## Pin Map

| Pin | Direction | Function | Notes |
|---|---|---|---|
| `7` | Input interrupt | SHT alert | ISR only sets `shtAlertPending`; I2C evaluation happens in loop code. |
| `10` | Input interrupt | DS3231 SQW/INT | ISR only sets `rtcAlarmPending`; RTClib alarm evaluation happens in loop code. |
| `2` | Output | Fan switch | Controls the 12 V fan switch stage. |
| `A1` | Input interrupt | Fan tach | 2N3904-conditioned inverted tach signal. |
| `3` | Output | Light power relay | Hard power switching for the grow light. |
| `4` | Output | AD5263 `SHDN` | External 10 kOhm pull-down; no internal pull-up. |
| `A0` | Analog input | Soil moisture | SEN0308 analog signal. |
| `9` | Input | CQRTSL25911 INT | Reserved and wired for future light-sensor interrupt use; firmware v1 polls over I2C and does not attach an ISR. |
| `SDA/SCL` | I2C | SHT31, DS3231, AT24C32, AD5263, CQRTSL25911 | Use 3.3 V-compatible pull-ups. |

## I2C Devices

| Device | Role | Address / Notes |
|---|---|---|
| SHT31-DIS-F | Temperature/humidity measurement and alert status | Address according to sensor wiring. |
| DS3231 | RTC and light schedule alarms | SQW/INT connected to `PIN_RTC_ALARM`. |
| AT24C32 | External EEPROM for persistent configuration | Fixed project address `0x57`. |
| AD5263BRUZ50 | Grow-light dimmer resistance path | Fixed project address `0x2C` (`AD0 = GND`, `AD1 = GND`). |
| CQRTSL25911 / TSL25911 | Cabinet illuminance measurement | Fixed sensor address `0x29`; INT line is wired to `PIN_LIGHT_SENSOR_INT` for future use. |

No I2C access may happen in ISRs.

## Cabinet Light Sensor Placement

The CQRTSL25911 is placed inside the plant cabinet, where it measures the light that actually reaches the cabinet environment. Because the sensor is under the grow light when the lamp is on, it is not a reliable standalone room ambient-light sensor during lamp operation.

In the initial firmware integration, this sensor is measurement-only:

- cabinet illuminance and raw light channels are published to Home Assistant
- the light schedule, dimming jobs, and fallback behavior are not changed by the sensor
- the INT line is physically prepared, but no interrupt-driven light-sensor logic is used yet

## Light Dimmer Signal Conditioning

The grow light is dimmed through an `AD5263BRUZ50` as a digitally adjustable resistance between `Dim+` and `Dim-`.

Hardware assumptions:

- AD5263BRUZ50 in I2C mode (`DIS = 1`)
- pull-ups on SDA/SCL to 3.3 V
- `SHDN` on `PIN_LIGHT_DIM_SHDN` with external 10 kOhm pull-down
- no internal pull-up on `SHDN`
- analog channel wiring: `Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-`
- minimal effective resistance / approximately 0 Ohm = 0 %
- maximal effective resistance / approximately 100 kOhm = 100 %

Reference mapping:

- `0 %  => W2 = 0,   W1 = 255`
- `50 % => W2 = 0,   W1 = 0`
- `100 % => W2 = 255, W1 = 0`

The relay remains responsible for hard switching the 230 V supply.

## Fan Tach Signal Conditioning

The fan tach signal is converted to 3.3 V logic for the Nano 33 IoT through a **2N3904 transistor stage**:

- fan tach with 10 kOhm pull-up to 9 V
- tach through 47 kOhm to the base of a 2N3904
- 100 kOhm from base to GND
- emitter to GND
- collector to `PIN_FAN_TACH`
- 10 kOhm pull-up from collector to 3.3 V

The resulting signal is inverted; firmware must select the interrupt edge accordingly.

## Safety Notes

- Do not rely on firmware as the only safety layer for mains wiring.
- Relay wiring, mains isolation, strain relief, fuse protection, and enclosure design must be handled physically.
- Keep water, soil, condensation, and low-voltage electronics physically separated from mains wiring.
- The AD5263 dimmer path is not galvanically isolated by firmware.
- Hardware tests that actuate the relay or light require a safe physical setup.

## Open Hardware Validation Items

Track concrete validation work in `ROADMAP.md`; keep only hardware context here.
