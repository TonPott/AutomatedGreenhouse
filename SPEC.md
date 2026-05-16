## Requirements List v5

## 1. Project Goal

The Arduino Nano 33 IoT controls a grow setup with:

- SHT3x temperature/humidity sensor
- 12V fan with tachometer signal
- dimmable grow light
- RTC_DS3231 + external AT24C32 EEPROM
- capacitive soil moisture sensor
- Home Assistant integration via MQTT

The system should cooperate with Home Assistant, but continue operating locally and autonomously if the connection is lost.

## 2. Hardware

### 2.1 Microcontroller
- Arduino Nano 33 IoT

### 2.2 Sensors And Actuators
- SHT3x via I²C with alert pin
- 3-pin 12V PC fan
- WINGONEER Tiny DS3231 AT24C32 I2C module
  - RTC via I²C with RTClib, type `RTC_DS3231`
  - external I²C EEPROM `AT24C32`
  - SQW/INT output for RTC alarms
- Grow Light:
  - dimming via a digitally adjustable resistance (AD5263) between `Dim+` and `Dim-`
  - hard shutoff via 3.3 V relay module
- DFRobot Capacitive Soil Moisture Sensor SEN0308
- WiFi + MQTT to Home Assistant

### 2.3 Hardware Documentation Requirement
The project documentation should also be understandable for users seeing the project for the first time.
The schematic in the project folder is the primary reference and should be explicitly mentioned in the text documents.

## 3. Pin Assignment

- SHT Alert: Pin 7
- RTC Alarm (SQW/INT): Pin 10
- Fan Switch: Pin 2
- Fan Tach: Pin A1
- Light Power Relay: Pin 3
- Light Dim SHDN: Pin 4
- Light Dimmer: I²C (SDA/SCL, AD5263 @ 0x2C)
- Soil Moisture: Pin A0

## 4. Temperature / Humidity / SHT3x
### 4.1 Measurement
The SHT3x periodically measures temperature and humidity.

These values should be:
- usable locally
- transmitted to Home Assistant

### 4.2 Thresholds
There are configurable thresholds:

- `TEMP_HIGH_SET`
- `TEMP_HIGH_CLEAR`
- `TEMP_LOW_SET`
- `TEMP_LOW_CLEAR`
- `HUM_HIGH_SET`
- `HUM_HIGH_CLEAR`
- `HUM_LOW_SET`
- `HUM_LOW_CLEAR`

These values should be:
- stored persistently in the external EEPROM
- written to the sensor at startup
- changeable from HA

### 4.3 Interrupt Logic
The SHT3x performs threshold monitoring.

When an alert occurs on pin 7, the status register is decoded. The relevant bits are especially:

- `alertTriggers[2]` = humidity tracking alert
- `alertTriggers[3]` = temperature tracking alert
- `alertTriggers[4]` = sensor reset detected

### 4.4 Reinitialization After Sensor Reset
If `alertTriggers[4]` is set, this should be used to reinitialize the sensor, or at least to write the configured thresholds to the sensor again.

### 4.5 Responsibility
- The FanController evaluates **no thresholds of its own**
- The SHT3x alert logic is authoritative

## 5. Fan

### 5.1 Automatic Mode
The fan is controlled based on the SHT3x alert logic:

- relevant alerts active → fan on
- clear state reached → fan off

### 5.2 Manual Mode
The fan should be manually switchable on and off in HA.

### 5.3 Auto Mode
There is an auto-mode switch for the fan:

- ON: sensor interrupts control the fan
- OFF: sensor interrupts are ignored, manual control remains active

### 5.4 RPM
The fan has 2 pulses per revolution.

RPM:
- measurement window 30 seconds
- only measure/publish if the fan should be running

### 5.5 Hardware Signal Conditioning For Tachometer
The documentation should include the used 2N3904 stage:

- tach line with 10 kΩ pull-up to 9 V
- tach through 47 kΩ to the base of a 2N3904
- 100 kΩ from base to GND
- emitter to GND
- collector to Arduino `PIN_FAN_TACH`
- 10 kΩ pull-up from collector to 3.3 V

The resulting signal is inverted.

## 6. Grow Light

### 6.1 Basic Structure
The light has two separate control paths:

- AD5263 dimmer path as variable resistance between `Dim+` and `Dim-`
- hard relay-off of the 230 V supply

### 6.2 Mandatory AD5263 Hardware

- Component: `AD5263BRUZ50`
- Mounted via TSSOP-24 adapter
- Supply:
  - `VDD = 12 V`
  - `VSS = GND`
  - `VL/VLOGIC = 3.3 V`
- Decoupling:
  - `100 nF` or `1 µF` between `VDD` and `VSS`
  - `100 nF` between `VL` and `GND`
- no galvanic isolation in the dimmer path

### 6.3 I²C Mode And Address

- `DIS = 1` for I²C mode
- `SDI/SDA` = SDA
- `CLK/SCL` = SCL
- `CS/AD0` and `RES/AD1` are the I²C address bits
- `AD0 = GND`
- `AD1 = GND`
- fixed 7-bit I²C address: `0x2C`
- pull-ups on the I²C lines to `3.3 V`

### 6.4 Mandatory Analog Channel Wiring

Only channel 1 and channel 2 of the AD5263 are used.

Mandatory signal path:

- `Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-`

### 6.5 Brightness Mapping To RDAC Values

The firmware continues to work logically with `0..100 %` brightness and maps internally to RDAC values.

Functional direction at the lamp dimmer input:

- low resistance between `Dim+` and `Dim-` = low brightness
- high resistance between `Dim+` and `Dim-` = high brightness
- minimal effective resistance / approximately `0 Ω` corresponds to `0 %`
- maximal effective resistance / approximately `100 kΩ` corresponds to `100 %`

Mandatory points:

- `0 % = W2 0, W1 255`
- `50 % = W2 0, W1 0`
- `100 % = W2 255, W1 0`

Mandatory intermediate logic:

- from `0 %` to `50 %`, channel 1 / `W1` changes first so the total resistance rises from the minimum value to the midpoint
- from `50 %` to `100 %`, channel 2 / `W2` changes afterwards so the total resistance rises from the midpoint to the maximum value
- on channel 1 the `A1-W1` stretch is used
- on channel 2 the `W2-B2` stretch is used

### 6.6 Resistance Limits

- target range at the light input: nominally approx. `0..100 kΩ`
- `0 Ω` is to be understood as minimal effective resistance or approximately `0 Ω`, because the AD5263 has residual resistance in rheostat mode
- practically approximated by the AD5263BRUZ50 with two channels in rheostat mode
- actually used lower/upper limits are set as project-internal firmware constants
- these dimmer limits are **not** configurable via HA

### 6.7 Two Control Worlds
A clean distinction must be maintained between:

- **Arduino Auto Schedule**
- **HA-controlled operation** (manual or by HA schedule)

The two must not control the light at the same time.

### 6.8 Light Modes

#### Mode A: Arduino Auto Mode ON
When `light_auto_mode = ON`:

- the internal Arduino schedule is active
- triggers from the HA schedule are ignored
- manual brightness adjustment is allowed, but only temporarily within the current time window
- at the next Arduino schedule event, the value is overwritten again
- the HA on/off switch for the light should have no effect in this mode or should be disabled

#### Mode B: Arduino Auto Mode OFF
When `light_auto_mode = OFF`:

- the Arduino schedule is inactive
- HA schedule triggers may control the light
- manual HA commands may control the light like a normal dimmable light
- on/off switch and brightness slider are active

### 6.9 Dimming Behavior
There is **no longer a single global fixed dimming ramp for all cases**.

Instead:

- Arduino schedule fades use the configured auto dim duration
- HA schedule commands use the duration provided by the command
- purely manual slider/switch operation in HA mode may make the light react immediately like a normal dimmable light

### 6.10 Hard Power Off
There is an additional separate HA switch/command for hard power off.

It should:

- switch only the relay immediately
- keep the internal dimmer state

This command is only relevant in HA-controlled operation.

### 6.11 SHDN Usage

- `SHDN` is actively used and pin 4 is used for it
- an external `10 kΩ` pull-down to GND is mandatory on `SHDN`
- because of this external pull-down, no internal pull-up is used for `SHDN`
- the AD5263 should remain in shutdown by default during reset/boot

### 6.12 Startup And Shutdown Sequence

Important:

- the AD5263 starts at midscale on power-on
- without a sequence, switching the relay on would otherwise cause an incorrect brightness start

Mandatory startup sequence:

1. AD5263 initially remains in `SHDN` during boot
2. Load configuration
3. Reconstruct target state
4. Set resistance value
5. Release `SHDN`
6. Only then close the relay

Mandatory shutdown sequence:

1. Open relay
2. Then put AD5263 into `SHDN`

### 6.13 Race Condition And Logic Rules For The Light
1. There is always exactly **one active light control source**:
   - Arduino Auto
   - or HA

2. When `light_auto_mode = ON`, HA schedule triggers are ignored.

3. When `light_auto_mode = OFF`, Arduino schedule events are ignored.

4. A new valid dimming request replaces any still-running old dimming request from the active control source.

5. When switching modes, a running dimming request is terminated; the new control source takes over only with its next valid command/event.

### 6.14 Fault Strategy For The Light Path

The AD5263 must be reachable before the relay is switched on and must provide a plausibly set target resistance. In case of AD5263 communication, write, or plausibility errors, the relay remains or is opened, the light stays off, and `light_fault` is set.

The technical retry/readback strategy belongs to the module description in `MODULES.md`.

Intended text values for `light_fault_reason`:

- `ad5263_not_found`
- `ad5263_write_failed`
- `ad5263_readback_mismatch`

## 7. Light Schedule Via DS3231 Alarms
### 7.1 Arduino-Internal Schedule
The Arduino stores internally:

- on / dim-on time
- off / dim-off time
- default dim duration

### 7.2 Time Format
Internal:
- minutes since midnight

### 7.3 RTC Alarm Usage
The Arduino-internal schedule should be implemented using the **two alarm registers of the DS3231**.

Requirements:
- the stored on/off times are written to the DS3231 alarm registers
- when an alarm occurs, an interrupt is triggered through the `SQW/INT` pin
- the ISR only sets a flag
- the main loop evaluates which alarm fired
- then the corresponding on/off dimming request is started

### 7.4 Library Requirement
It must be checked and used in the implementation that RTClib provides the required functions for `RTC_DS3231`, especially:

- `writeSqwPinMode(...)`
- `setAlarm1(...)`
- `setAlarm2(...)`
- `alarmFired(...)`
- `clearAlarm(...)`

### 7.5 Pull-Up Of The Alarm Line
Because `SQW/INT` is open drain, at least `INPUT_PULLUP` should be used on the Arduino side.
If the real line is unstable, an additional external pull-up to 3.3 V may be provided.

### 7.6 Distinction From HA Schedule
Section 7 applies only to the **Arduino-internal schedule**.

An additional HA schedule is separate from it and only effective when `light_auto_mode = OFF`.

## 8. Soil Moisture

### 8.1 Measurements
The following should be available:
- `sensor.soil_moisture_percent`: calculated soil moisture in %
- `sensor.soil_moisture_raw`: raw sensor value

### 8.2 Calibration Parameters
- `number.soil_air`
- `number.soil_water`
- `number.soil_depth_mm`

These values:
- are stored persistently
- are shown in HA
- can be changed in HA

`soil_air` and `soil_water` remain user-facing persistence values in the expected project range:

- min: 0
- max: 1000
- step: 1

Separately, the firmware sensor module may defensively clamp internal ADC raw values to the 12-bit safety range `0..4095`.

`soil_depth_mm` is an active correction parameter for percent calculation, not only an informational value.

### 8.3 Calibration
The calibration routine runs fully in HA.
The firmware does not manage a calibration assistant and does not contain an internal calibration state machine.

Therefore the firmware does not need to:
- know the calibration step
- pause measurements
- maintain an internal calibration status
- store individual intermediate steps

The firmware only needs to:
- periodically read the raw value
- periodically calculate the percent value from `soil_air`, `soil_water`, current raw value, and `soil_depth_mm`
- publish `sensor.soil_moisture_raw`
- publish `sensor.soil_moisture_percent` if the evaluation is valid
- provide the raw value on request
- accept final changes to `number.soil_air`, `number.soil_water`, and `number.soil_depth_mm`
- store changed persistence values in the external EEPROM

### 8.4 HA Operation
One firmware button is sufficient:

- `button.read_soil_raw_value`

Separate firmware buttons such as `capture_soil_air` or `capture_soil_water` should not be introduced. The existing generic button plus HA scripts/automations is sufficient and avoids unnecessary MQTT entities.

Intended HA-supported workflow:

1. Air step:
   - The user places the sensor dry in air.
   - HA calls `button.read_soil_raw_value`.
   - HA waits briefly until `sensor.soil_moisture_raw` is updated.
   - HA writes this value to `number.soil_air`.

2. Water step:
   - The user places the sensor in water at the reference depth of `120 mm`.
   - HA calls `button.read_soil_raw_value`.
   - HA waits briefly until `sensor.soil_moisture_raw` is updated.
   - HA writes this value to `number.soil_water`.

3. Depth step:
   - The user manually enters the actual insertion depth in the substrate in `number.soil_depth_mm`.
   - The firmware actively uses this value for the corrected percent calculation.

### 8.5 Depth Correction And Percent Calculation
The sensor response depends on how much active sensor area is actually inside the medium. Therefore the firmware uses a simple linear depth correction.

Definitions:

- `SOIL_REFERENCE_DEPTH_MM = 120`
- `soil_air`: raw value with sensor completely in air
- `soil_water`: raw value with sensor in water at 120 mm reference depth
- `soil_depth_mm`: actual insertion depth in the substrate

Concept:

```text
depth_factor = soil_depth_mm / SOIL_REFERENCE_DEPTH_MM
percent = (soil_air - raw) / ((soil_air - soil_water) * depth_factor) * 100
```

The air reference corresponds to `0 %`, and the water reference at `120 mm` corresponds to `100 %`. Valid results are constrained to `0..100 %`. The formula is a deliberate linear approximation; a measurement series exists, but the project currently accepts this model as sufficiently accurate.

### 8.6 Invalid Depth
Values below `20 mm` are considered invalid for soil moisture percent calculation.

Reason:

- The first physical sensor marking is at `20 mm`.
- Measurements below that are considered unreliable.

Firmware behavior:

- If `soil_depth_mm < 20`, `sensor.soil_moisture_percent` should be treated as invalid or unavailable.
- `sensor.soil_moisture_raw` may still be published.
- In this case, the firmware should not produce a misleading corrected percent value.

### 8.7 Measurement Interval
The soil moisture sensor should be read:
- every **10 seconds**
- and published to HA

It is useful to publish both:
- the raw value
- and the calculated percent value

## 9. RTC / Time / EEPROM

### 9.1 RTC Type
- `RTC_DS3231`

### 9.2 External EEPROM
Persistent configuration should not be stored in Arduino flash, but in the **AT24C32** on the RTC module.
Access to this EEPROM is performed through the **JC_EEPROM** library (which requires the **Streaming** library).
The I²C address of the AT24C32 EEPROM on the WINGONEER Tiny DS3231 AT24C32 I2C module has been verified in the real build and is fixed for this project.
It is 0x57.

Requirements:
- The firmware should use this known EEPROM I²C address directly.
- The EEPROM address should not be guessed, scanned, or searched automatically at runtime.
- The address is defined centrally in `Config.h` or a comparable configuration file.
- The persistence layer uses this fixed address for all read/write access through `JC_EEPROM`.
- The **JC_EEPROM** library requires the **Streaming** library as a dependency.

Reason:
- The EEPROM was accessed successfully with `JC_EEPROM` in the test build.
- This removes unnecessary auto-detection and keeps initialization simpler and more robust.

### 9.3 Usage
The RTC is the local time base for:
- Arduino light schedule
- autonomous operation when the connection is lost

The RTC alarms are the trigger source for the Arduino-internal light schedule.

### 9.4 Synchronization
The time should be synchronized:
- during boot
- at least 1× per day afterwards

Optionally additionally:
- manual sync via HA button

### 9.5 NTP
The firmware should actively synchronize via NTP and set the RTC from it.

After successful time sync, the DS3231 alarm registers should be rewritten.

## 10. Home Assistant / MQTT

### 10.1 Library
Used library:
- Arduino Home Assistant Integration by Dawid Chyrzynski
- Reference: `https://github.com/dawidchyrzynski/arduino-home-assistant`

### 10.2 Device
The Arduino appears as one HA device.

### 10.3 HA Entities

#### Sensors
- `temperature`
- `humidity`
- `soil_moisture_percent`
- `soil_moisture_raw`
- `fan_rpm`
- `light_fault`
- `fan_fault`
- `sht_fault`
- `rtc_fault`
- `eeprom_fault`
- `light_fault_reason`

Type mapping in HA:

- `binary_sensor.light_fault`
- `binary_sensor.fan_fault`
- `binary_sensor.sht_fault`
- `binary_sensor.rtc_fault`
- `binary_sensor.eeprom_fault`
- `sensor.light_fault_reason` (text)

#### Switches
- `fan`
- `fan_auto_mode`
- `light_auto_mode`
- `light_hard_power_off`
- `light_fallback_to_auto`

#### Light
- `grow_light`
  - Brightness
  - ON/OFF
  - fully usable only when `light_auto_mode = OFF`

#### Number Entities
- temperature thresholds
- humidity thresholds
- Arduino light schedule times
- Arduino default dim duration
- `soil_air`
- `soil_water`
- `soil_depth_mm`
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`

#### Buttons
- `sync time`
- `read_soil_raw_value`
- `start_ha_dim`

### 10.4 HA Dimming Request
For the HA schedule dimming job, operation is fixed as follows:

- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

Mandatory behavior:

- These three entities are used only for **HA schedule operation**
- They are effective only when `light_auto_mode = OFF`
- Pressing `button.start_ha_dim` starts a dimming request on the Arduino:
  - from the **current actual state**
  - to `ha_dim_target_percent`
  - within `ha_dim_duration_minutes`
- If `light_auto_mode = ON`, this dimming request is ignored

Persistence rule:

- The Number entities remain runtime/command parameters and are not durable configuration.
- If a HA dimming job is relevant after restart, its resumption is reconstructed through the `Light Resume State` (start/target/duration/start time on RTC/Epoch basis), not through `millis()`.

### 10.5 State Restoration
Separate sensors for actual light brightness or light mode are not required as long as:

- the state of the `light` entity is published correctly
- the `switch` states are published correctly
- after startup or MQTT reconnect, the current states are actively reported to HA again

## 11. Network / Connection Behavior

### 11.1 Connection
The device connects to:
- WiFi
- MQTT broker

### 11.2 Credentials
All credentials are in `Credentials.h`, at least:
- WiFi SSID
- WiFi password
- MQTT Host
- MQTT Port
- MQTT User
- MQTT password
- Device Name / ID / Prefix
- NTP server
- timezone / offset if needed

### 11.3 Availability / LWT
The device reports availability via MQTT and uses LWT.

### 11.4 Reconnect
On WiFi/MQTT outage, reconnect is automatic.

### 11.5 Fallback After Connection Loss
If the connection is not restored for more than 10 minutes, a configurable behavior should apply for the light:

- turn light off
- or switch to internal Arduino Auto Mode

This behavior should be configurable via HA.

Other local functions such as sensors, RTC, EEPROM, and fan logic continue running.

## 12. Persistence

### 12.1 Storage Location
Used storage:

- `AT24C32` on the RTC module
- access through **JC_EEPROM**

### 12.2 Already Existing Persistent Configuration (Reuse)
Already existing and **not** to be created a second time:

- `lightAutoMode`
- `fanAutoMode`
- `lightOnTimeMinutes`
- `lightOffTimeMinutes`
- `defaultLightDimMinutes`
- `lightFallbackMode`
- additional sensor/soil/threshold values

### 12.3 Light Resume State (Add New)
In addition, a small resume state is required for target-state reconstruction, at least with:

- last effective brightness
- `hardPowerOffActive`
- flag whether a HA dimming job was active
- start brightness
- target brightness
- job duration
- job start time on RTC/Epoch basis

### 12.4 Time Base For Resumption

- `millis()` is not sufficient for restart resumption.
- Correct reconstruction after restart requires an RTC/Epoch-based time reference.

### 12.5 Write Behavior

- Write only when values actually changed.
- Design resume-state writes so unnecessary EEPROM load is avoided.

## 13. Architecture
### 13.1 Non-Blocking
- no long `delay()` calls
- control via `millis()`
- short I²C waits are allowed

### 13.2 Module Boundaries
- SHTa: sensor + alert evaluation
- FanController: fan switching + RPM measurement
- LightController: AD5263/relay + dimming requests
- ClockService: DS3231 + alarm management + NTP sync
- HAInterface: HA entities + commands
- NetworkManager: WiFi/MQTT + reconnect
- Persistence: external EEPROM

### 13.3 Fault States (Mandatory)
The following fault states are mandatory in the firmware documentation:

- `light_fault`
- `fan_fault`
- `sht_fault`
- `rtc_fault`
- `eeprom_fault`

In HA, these fault flags are published as `binary_sensor`.

Do not use:

- `sensor_fault`
- `system_fault`
- `last_fault_code`

Additional text state:

- `light_fault_reason`

In HA, `light_fault_reason` is published as a text `sensor`.

Intended contents of `light_fault_reason`:

- `ad5263_not_found`
- `ad5263_write_failed`
- `ad5263_readback_mismatch`

Mandatory behavior rules:

- AD5263 not reachable during boot: relay open, light off, set `light_fault`, `light_fault_reason = ad5263_not_found`.
- AD5263 error during operation: open relay, set `light_fault`, update `light_fault_reason`.
- The concrete retry/readback strategy is documented in `MODULES.md`.
- Fan fault: if the fan should effectively be on but no tach pulses are present after the grace period, set `fan_fault`.
- Do not claim strong hardware self-diagnostics for relay and soil moisture sensor, because there is no real feedback channel.

## 14. Reused Existing Code
### 14.1 SHTa
The following should especially be reused:

- `begin()`
- `startPeriodicMeasurement()`
- `blockingReadMeasurement()`
- `decodeStatusRegister()`
- `clearStatus()`
- `writeHighLimit()`
- `writeHighClear()`
- `writeLowLimit()`
- `writeLowClear()`
- `readLimit()`
- `limitPrinter()`
- `crc8()`
- `encodeAlertLimit()`

### 14.2 Alert Structure
- `alertTriggers[]` remains as status container
