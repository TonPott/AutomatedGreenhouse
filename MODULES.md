## Technical Module Specification v4

## 1. Overall Architecture

The project is structured as an Arduino sketch folder with `.ino`, `.h`, and `.cpp` files.
The main sketch is located at `Smaeenhouse/Smaeenhouse.ino`; the related module and configuration files are located in the `Smaeenhouse/` folder.

Recommended files:

- `Smaeenhouse/Smaeenhouse.ino`
- `Smaeenhouse/Config.h`
- `Smaeenhouse/Credentials.h` (local, do not commit to the repository)
- `Smaeenhouse/PersistentConfig.h/.cpp`
- `Smaeenhouse/RtcEepromStorage.h/.cpp`
- `Smaeenhouse/SHTa.h/.cpp`
- `Smaeenhouse/FanController.h/.cpp`
- `Smaeenhouse/LightController.h/.cpp`
- `Smaeenhouse/MoistureSensor.h/.cpp`
- `Smaeenhouse/ClockService.h/.cpp`
- `Smaeenhouse/NetworkManager.h/.cpp`
- `Smaeenhouse/HAInterface.h/.cpp`

### Basic Principle
Each module should preferably have this structure:

- `begin()`
- `update(uint32_t nowMs)`

`loop()` then only calls the `update()` methods.

## 2. Config.h

### Purpose
Central compile-time constants.

### Contents
- pin assignment
- default values
- measurement intervals
- dimmer resistance limits / brightness mapping
- default thresholds
- default schedule
- default soil calibration
- hardware notes for signal conditioning

### Typical Constants
- `PIN_SHT_ALERT`
- `PIN_RTC_ALARM`
- `PIN_FAN_SWITCH`
- `PIN_FAN_TACH`
- `PIN_LIGHT_DIM_SHDN`
- `PIN_LIGHT_POWER`
- `PIN_SOIL_SENSOR`

- `AD5263_I2C_ADDRESS = 0x2C`
- `AD5263_AD0_TO_GND = true`
- `AD5263_AD1_TO_GND = true`

- channel-specific effective RDAC limits for the used dimmer channels (`W2` and `W1`), for example:
  - `LIGHT_DIM_W2_RDAC_MIN_EFFECTIVE`
  - `LIGHT_DIM_W2_RDAC_MAX_EFFECTIVE`
  - `LIGHT_DIM_W1_RDAC_MIN_EFFECTIVE`
  - `LIGHT_DIM_W1_RDAC_MAX_EFFECTIVE`
- `LIGHT_DIM_MAPPING_SPLIT_PERCENT = 50` (split between W2 phase and W1 phase)
- Note: The identifiers are documented as examples; mandatory are the separate per-channel limits and the direction `minimal resistance = 0 %`, `maximal resistance = 100 %`.

- `DEFAULT_TEMP_HIGH_SET`
- `DEFAULT_TEMP_HIGH_CLEAR`
- `DEFAULT_HUM_HIGH_SET`
- `DEFAULT_HUM_HIGH_CLEAR`

- `DEFAULT_LIGHT_ON_TIME_MINUTES`
- `DEFAULT_LIGHT_OFF_TIME_MINUTES`
- `DEFAULT_LIGHT_DIM_MINUTES`

- `DEFAULT_SOIL_AIR`
- `DEFAULT_SOIL_WATER`
- `DEFAULT_SOIL_DEPTH_MM`
- `SOIL_REFERENCE_DEPTH_MM = 120`
- `SOIL_MIN_VALID_DEPTH_MM = 20`
- `SOIL_ADC_MIN = 0`
- `SOIL_ADC_MAX = 4095`

These soil constants complement the existing soil default values and do not replace them:

- `SOIL_REFERENCE_DEPTH_MM` is the reference depth at which water corresponds to `100 %`.
- `SOIL_MIN_VALID_DEPTH_MM` is the lower limit below which the calculated percent value becomes invalid/unavailable.
- `SOIL_ADC_MIN` and `SOIL_ADC_MAX` are internal defensive limits for ADC raw values.

- `SOIL_PUBLISH_INTERVAL_MS = 10000`
- `TEMP_HUM_PUBLISH_INTERVAL_MS = 60000`
- `FAN_RPM_PUBLISH_INTERVAL_MS = 30000`

- `MQTT_FALLBACK_TIMEOUT_MS = 600000`

## 3. Credentials.h
### Purpose
Central storage for all credentials and installation-specific IDs.

### Contents
- WiFi SSID
- WiFi password
- MQTT host
- MQTT port
- MQTT user
- MQTT password
- Device Name
- Device Unique ID
- MQTT Prefix
- NTP server
- timezone information

## 4. PersistentConfig / External EEPROM Persistence

### Purpose
Persistent storage of all mutable runtime parameters in the **AT24C32 EEPROM**
of the WINGONEER Tiny DS3231 AT24C32 I2C module through the **JC_EEPROM** library.

### Data Structure
A central struct, for example `PersistentConfigData`, with:

- temp/hum thresholds
- Arduino light times
- Arduino dim duration
- `fanAutoMode`
- `lightAutoMode`
- fallback light behavior
- Soil Air/Water/Depth
- resume state for light recovery (separate substructure or clearly separated block) with:
  - last effective brightness
  - `hardPowerOffActive`
  - flag whether a HA dimming job was active
  - job start brightness
  - job target brightness
  - job duration
  - job start time on RTC/Epoch basis
- version number
- CRC/validation

### API
- `bool begin(TwoWire& wire = Wire);`
- `bool configureEeprom();`
- `bool load(PersistentConfigData& out);`
- `bool save(const PersistentConfigData& cfg);`
- `bool saveIfChanged(const PersistentConfigData& cfg);`
- `void applyDefaults(PersistentConfigData& cfg);`

### Requirements
- no unnecessary EEPROM writes
- save changes only when the value actually changed
- robust initialization when storage is empty/invalid
- encapsulate JC_EEPROM in project-specific logic, do not use it directly across the project
- never access EEPROM from an ISR
- The EEPROM I²C address is known and fixed for the project and is documented as '0x57'.
- The implementation should use this fixed address directly.
- There should be no automatic address search and no I²C scan during normal firmware initialization.
- The address is defined centrally as a constant, for example in `Config.h`.

### Technical Consequence
- The EEPROM wrapper or persistence class initializes `JC_EEPROM` directly with the known device address.
- Error handling refers to communication errors or invalid data, not to address search.
- Do not create existing configuration values a second time; extend persistence specifically with resume state.
- `millis()` is not sufficient for restart resumption; resume state requires an RTC/Epoch time base.

## 5. SHTa

### Purpose
Encapsulation of the SHT3x including threshold programming and alert evaluation.

### Reused Functions
- `begin()`
- `startPeriodicMeasurement(...)`
- `blockingReadMeasurement(...)`
- `decodeStatusRegister()`
- `clearStatus()`
- `writeHighLimit(...)`
- `writeHighClear(...)`
- `writeLowLimit(...)`
- `writeLowClear(...)`
- `readLimit(...)`
- `limitPrinter()`

### Additional Functional Responsibility
- writing configured thresholds to the sensor
- evaluating `alertTriggers[]`
- detecting sensor reset through `alertTriggers[4]`
- re-applying thresholds after reset

### API Extensions
Recommended:

- `void applyThresholdConfig(const PersistentConfigData& cfg);`
- `bool readMeasurement(float& temp, float& hum);`
- `bool hasTempAlert() const;`
- `bool hasHumAlert() const;`
- `bool hasSensorReset() const;`
- `void clearDecodedFlags();`

### ISR Rule
The actual I²C communication should **not happen in the ISR**.

Instead:
- ISR only sets a `volatile bool shtAlertPending`
- the main logic then calls `decodeStatusRegister()`

## 6. FanController

### Purpose
Switching the fan and measuring RPM.

### Responsibilities
- set fan switch pin
- count tach pulses for RPM
- implement auto/manual logic
- no own threshold calculations

### States
- `fanAutoMode`
- `fanManualState`
- `fanEffectiveState`
- `rpm`
- `fanFault`
- `tachPulseCount`

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `void setAutoMode(bool enabled);`
- `void setManualState(bool on);`
- `void setAutoDemand(bool on);`
- `bool isOn() const;`
- `uint16_t getRPM() const;`

### Control Logic
Effective fan state:

- if `fanAutoMode == true`: state follows `autoDemand`
- if `fanAutoMode == false`: state follows `manualState`

`autoDemand` is set **outside** the module from the SHT alert logic.

### RPM Logic
- tach ISR counts pulses
- calculate RPM every 30 s
- only when the fan should effectively be running
- set RPM to 0 on standstill if appropriate
- if the fan should effectively be on but no tach pulses arrive after the grace period: set `fanFault`

### Hardware Note: Fan Tach
The text documentation and the schematic should document the used 2N3904 stage:

- fan tach through 10 kΩ pull-up to 9 V
- tach through 47 kΩ to the base of a 2N3904
- 100 kΩ from base to GND
- emitter to GND
- collector to `PIN_FAN_TACH`
- 10 kΩ pull-up from collector to 3.3 V

The signal is inverted; the code selects the interrupt edge accordingly.

## 7. LightController

### Purpose
Control of AD5263 dimmer, SHDN pin, relay, and dimming requests.

### Central Responsibility
- clear separation between Arduino Auto and HA control
- management of the current brightness state (`0..100 %`)
- execution of dimming jobs
- AD5263 control via I²C outside ISRs
- use two AD5263 channels according to the fixed signal path
- SHDN control with safe boot behavior
- separate hard relay-off

### Mandatory Hardware Assumptions
- AD5263BRUZ50 in I²C mode (`DIS = 1`)
- fixed I²C address `0x2C` (`AD0 = GND`, `AD1 = GND`)
- pull-ups on SDA/SCL to `3.3 V`
- SHDN on `PIN_LIGHT_DIM_SHDN` with external `10 kΩ` pull-down
- no internal pull-up on SHDN
- analog channel wiring: `Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-`
- lamp dimmer input: minimal effective resistance / approximately `0 Ω` = `0 %`, maximal effective resistance / approximately `100 kΩ` = `100 %`

### Brightness Mapping (Mandatory)

Reference points:

- `0 %  => W2=0,   W1=255`
- `50 % => W2=0,   W1=0`
- `100 % => W2=255, W1=0`

Intermediate ranges:

- `0..50 %`: first change channel 1 / `W1` so the total resistance rises from the minimum value to the midpoint
- `50..100 %`: then change channel 2 / `W2` so the total resistance rises from the midpoint to the maximum value
- used rheostat stretches: `A1-W1` on channel 1 and `W2-B2` on channel 2

Dimmer limits:

- effective lower/upper limits are constrained through firmware constants
- `0 Ω` is to be understood as minimal effective resistance or approximately `0 Ω`, because the AD5263 has residual resistance in rheostat mode
- these limits are not configurable via HA

### Boot And Shutdown Sequence (Mandatory)

On startup:

1. Keep AD5263 in `SHDN`
2. Load configuration
3. Reconstruct target state from persisted data + resume state
4. Set RDAC resistance values
5. Release `SHDN`
6. Only then close relay

On shutdown:

1. Open relay
2. Put AD5263 into `SHDN`

Note:

- The AD5263 starts at midscale; without this sequence, an incorrect brightness start is possible.

### Important Internal States
- `lightAutoMode`
- `currentBrightnessPercent`
- `targetBrightnessPercent`
- `powerCutActive`
- `controlSource`:
  - `ARDUINO_AUTO`
  - `HA_CONTROL`
- current dimming job:
  - active/inactive
  - start brightness
  - target brightness
  - start time
  - duration
  - source
- `lightFault`
- `lightFaultReason`

### Resume State (Persisted)
At minimum:

- last effective brightness
- `hardPowerOffActive`
- flag whether a HA dimming job was active
- job start brightness
- job target brightness
- job duration
- job start time (RTC/Epoch-based)

Important:

- Resumption after restart must not be based only on `millis()`.

### API
- `void begin();`
- `void update(uint32_t nowMs);`

- `void setAutoMode(bool enabled);`
- `bool isAutoMode() const;`

- `void applyManualBrightness(uint8_t percent);`
- `void applyManualOnOff(bool on);`
- `void applyHardPowerOff(bool off);`

- `void startArduinoDimJob(uint8_t targetPercent, uint32_t durationMs);`
- `void startHADimJob(uint8_t targetPercent, uint32_t durationMs);`

- `uint8_t getCurrentBrightness() const;`
- `bool isPowerOn() const;`
- `bool hasLightFault() const;`
- `const char* getLightFaultReason() const;`

### Detailed Rules

#### With Auto Mode ON
- ignore `startHADimJob()`
- ignore `applyManualOnOff()` or treat it as no-op
- `applyManualBrightness()` may optionally override the current target value within the time window

#### With Auto Mode OFF
- ignore Arduino schedule events
- accept HA schedule dimming jobs and manual operation

#### Dimming Job Behavior
- a new job replaces the old job from the same active control world
- brightness is interpolated linearly over the runtime
- if `durationMs == 0`, set immediately
- at target `0 %`, power-off may optionally be set unless this is explicitly the separate hard-power-off switch

#### Hard Power Off
- only switches the relay
- does not change the stored brightness value

#### Fixed HA Dimming Job Interface
The HA dimming job is triggered mandatorily through these entities:

- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

### LightController Fault Strategy

Mandatory readback/plausibility strategy (staged and rate-limited):

1. Boot phase before `SHDN` release and before relay ON:
   - address AD5263
   - write defined W1/W2 RDAC values
   - check readback once
   - continue only if the result is consistent

2. For discrete commands:
   - manual brightness jump
   - start Arduino dimming job
   - start HA dimming job
   - restore from resume state
   in each case:
   - write
   - optional 1 short retry
   - check readback

3. For long dimming ramps:
   - no readback on every interpolation step
   - readback verification at the end of the ramp

Fault response (mandatory):

- AD5263 not reachable during boot:
  - relay remains open
  - light stays off
  - `lightFault = true`
  - `lightFaultReason = "ad5263_not_found"`

- AD5263 write failure after retry:
  - open relay
  - `lightFault = true`
  - `lightFaultReason = "ad5263_write_failed"`

- AD5263 readback mismatch after retry:
  - open relay
  - `lightFault = true`
  - `lightFaultReason = "ad5263_readback_mismatch"`

## 8. MoistureSensor

### Purpose
Reading and converting the soil moisture sensor.

### Responsibilities
- read raw value
- calculate calibrated percent value from Air/Water/Depth
- periodic 10 s measurement
- detect invalid percent values when insertion depth is too low

### States
- `soilAir`
- `soilWater`
- `soilDepth`
- `lastRaw`
- `lastPercent`
- `lastPercentValid` or equivalent validity flag
- `lastReadMs`

### API
- `void begin(int air, int water, int depthMm);`
- `void update(uint32_t nowMs);`
- `int readRawNow();`
- `int getLastRaw() const;`
- `uint8_t getLastPercent() const;`
- `bool isLastPercentValid() const;`
- `void setCalibration(int air, int water, int depthMm);`

### Rules
- `update()` reads every 10 s
- `readRawNow()` reads immediately, updates `lastRaw`, and enables `button.read_soil_raw_value`
- internal ADC raw values may defensively be constrained to `SOIL_ADC_MIN..SOIL_ADC_MAX` (`0..4095`)
- `soilAir` and `soilWater` remain persistent HA configuration values in the expected project range `0..1000` with step `1`
- `soilDepth` is an active correction parameter, not only an informational value
- if `soilDepth < SOIL_MIN_VALID_DEPTH_MM`, the percent value is invalid/unavailable; the raw value may still be published
- no knowledge of the HA calibration routine is required
- no firmware buttons such as `capture_soil_air` or `capture_soil_water`

### Depth Correction
Definitions:

- `SOIL_REFERENCE_DEPTH_MM = 120`
- `SOIL_MIN_VALID_DEPTH_MM = 20`
- `soilAir`: raw value with sensor completely in air
- `soilWater`: raw value with sensor in water at 120 mm reference depth
- `soilDepth`: actual insertion depth in mm

Concept:

```text
depth_factor = soilDepth / SOIL_REFERENCE_DEPTH_MM
percent = (soilAir - raw) / ((soilAir - soilWater) * depth_factor) * 100
```

The air reference corresponds to `0 %`, and the water reference at `120 mm` corresponds to `100 %`. Valid percent values are constrained to `0..100 %`. The linear correction is intentionally sufficient for this project.

## 9. ClockService

### Purpose
Management of RTC_DS3231, its alarms, and NTP synchronization.

### Responsibilities
- initialize RTC
- fetch NTP time
- set RTC
- provide current time
- configure DS3231 SQW/INT for alarm operation
- mirror daily alarm times into the RTC register
- evaluate and acknowledge alarm flags
- trigger daily re-sync

### States
- `lastSuccessfulSyncMs`
- `rtcAvailable`
- `timeValid`
- `alarmsConfigured`

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `bool syncFromNTP();`
- `DateTime now() const;`
- `uint16_t minutesSinceMidnight() const;`
- `bool isTimeValid() const;`
- `bool configureScheduleAlarms(uint16_t onMinutes, uint16_t offMinutes);`
- `ClockAlarmEvent serviceAlarmFlags();`

### Recommended Alarm Event Type
```cpp
enum class ClockAlarmEvent {
    None,
    LightOnAlarm,
    LightOffAlarm
};
```

### Implementation Notes
- Use RTClib functions of the DS3231:
  - `writeSqwPinMode(...)`
  - `setAlarm1(...)`
  - `setAlarm2(...)`
  - `alarmFired(...)`
  - `clearAlarm(...)`
- SQW/INT is an open-drain output; operate the Arduino side with `INPUT_PULLUP`.
- If the line proves noise-sensitive in the real build, an additional external 10 kΩ pull-up to 3.3 V may be added.

### Rules
- sync during boot
- afterwards at least every 24 h
- manual sync command from HA possible
- reprogram alarm registers after time sync or configuration change
- I²C access only outside the ISR

## 10. NetworkManager

### Purpose
Manage WiFi and MQTT connection.

### Responsibilities
- connect WiFi
- connect MQTT
- reconnect strategy
- measure offline duration
- detect fallback condition after 10 min

### States
- `wifiConnected`
- `mqttConnected`
- `lastConnectionOkMs`
- `fallbackActive`

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `bool isWifiConnected() const;`
- `bool isMqttConnected() const;`
- `bool isOnline() const;`
- `bool isFallbackActive() const;`

### Rules
- periodic reconnect attempts
- online = WiFi + MQTT ok
- if offline >10 min: `fallbackActive = true`
- on successful connection again: `fallbackActive = false`

## 11. HAInterface

### Purpose
Mapping of all HA entities and processing of HA commands.

### Responsibilities
- Discovery / device setup
- publishing states and measurements
- receiving commands
- state re-publish after MQTT reconnect

### Required References
The module should be able to access other modules, for example by reference in the constructor:

- `FanController&`
- `LightController&`
- `MoistureSensor&`
- `ClockService&`
- `SHTa&`
- `PersistentConfigManager&`
- `NetworkManager&`

### HA Entities
- `HADevice`
- `HAMqtt`
- `HASensorNumber` for temperature, humidity, `sensor.soil_moisture_percent`, `sensor.soil_moisture_raw`, RPM
- `HABinarySensor` for `light_fault`, `fan_fault`, `sht_fault`, `rtc_fault`, `eeprom_fault`
- `HASensor` (text) for `light_fault_reason`
- `HASwitch` for Fan, FanAuto, LightAuto, HardPowerOff, fallback behavior
- `HALight` for Grow-Light
- `HANumber` for thresholds, times, dim duration, `number.soil_air`, `number.soil_water`, `number.soil_depth_mm`
- `HANumber` for `ha_dim_target_percent`
- `HANumber` for `ha_dim_duration_minutes`
- `HAButton` for `sync_time`
- `HAButton` for `read_soil_raw_value`
- `HAButton` for `start_ha_dim`

Separate soil capture buttons such as `capture_soil_air` or `capture_soil_water` are not part of the HA entity model. HA uses `read_soil_raw_value` and writes the read value into `soil_air` or `soil_water` by script/automation.

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `void onMqttConnected();`

### Additional Helper Functions
- `void publishAllStates();`
- `void publishSensorValues();`
- `void publishConfigValues();`

### Additional Internal States For HA Dimming Job
- `uint8_t pendingHaDimTargetPercent;`
- `uint16_t pendingHaDimDurationMinutes;`

### Rules
- on reconnect, publish all relevant states again
- light and switch states are sufficient; no separate sensor for actual light brightness or light mode is required
- `start_ha_dim` validates:
  - target 0..100
  - duration 0..1440 minutes
- then calls:
  - `lightController.startHADimJob(target, durationMs)`

### Persistence Note
The values for:
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`

are **not persistent**, but only runtime/command parameters.
- A running HA dimming job can be made reconstructable after restart through the persisted resume state (start/target/duration/start time).

### System-Level Fault Status
The firmware documentation uses the following fault states:

- `light_fault`
- `fan_fault`
- `sht_fault`
- `rtc_fault`
- `eeprom_fault`

Do not use:

- `sensor_fault`
- `system_fault`
- `last_fault_code`

Additional text status (as text `sensor`):

- `light_fault_reason`

## 12. Smaeenhouse.ino

### Purpose
Central orchestration.

### Responsibilities
- global module instances
- setup order
- ISR flags
- call order in `loop()`

### Setup Order
1. Serial
2. Wire
3. Load persistent configuration from AT24C32 or defaults
4. Construct/initialize modules
5. Apply SHT thresholds
6. Initialize RTC and configure alarms
7. Start network/HA
8. Try first NTP sync
9. Enable interrupts

### Loop Order
1. `nowMs = millis()`
2. `network.update(nowMs)`
3. `clock.update(nowMs)`
4. check `sht` alert flag and evaluate status
5. from that, call `fan.setAutoDemand(...)`
6. check `rtcAlarmPending` and run `clock.serviceAlarmFlags()`
7. from that, start `light.startArduinoDimJob(...)` if needed
8. `fan.update(nowMs)`
9. `light.update(nowMs)`
10. `moisture.update(nowMs)`
11. `ha.update(nowMs)`

## 13. ISR Strategy

There should be three ISR-adjacent flags:

- `volatile bool shtAlertPending`
- `volatile bool rtcAlarmPending`
- tach ISR in FanController only for pulse counting

Important:
- no I²C operation in ISR
- no MQTT/HA operation in ISR
- no expensive logic in ISR

## 14. Logical Core Flows

### SHT Alert → Fan
1. Pin 7 interrupt sets flag
2. Main loop reads sensor status
3. If reset bit is set → write thresholds again
4. If temp or hum alert active → `fan.setAutoDemand(true)`
5. If no relevant alerts are active → `fan.setAutoDemand(false)`

### RTC Alarm → Arduino Light Schedule
1. DS3231 SQW/INT on `PIN_RTC_ALARM` sets interrupt flag
2. Main loop calls `clock.serviceAlarmFlags()`
3. `alarmFired(1)` / `alarmFired(2)` determine which alarm fired
4. Alarm flag is cleared
5. Depending on the event, the corresponding Arduino dimming request is started

### HA Light Operation
1. If `lightAutoMode == OFF`, LightController accepts HA commands
2. HA schedule starts explicit dimming jobs
3. Manual slider/on-off commands act directly

### Fallback
1. NetworkManager detects offline >10 min
2. depending on configuration:
   - turn light off
   - or activate internal Auto Mode
3. local functions continue running
