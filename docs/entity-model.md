# Home Assistant Entity Model

This file describes the planned Home Assistant entities and their meaning.

## Device

A single HA device:

- Name: `Grow Controller`
- Platform: ArduinoHA / MQTT Discovery

## Sensors

### Temperature
- Entity type: `sensor`
- Name: `temperature`
- Direction: Arduino → HA
- Unit: `°C`

### Humidity
- Entity type: `sensor`
- Name: `humidity`
- Direction: Arduino → HA
- Unit: `%`

### Soil Moisture Percent
- Entity type: `sensor`
- Name: `soil_moisture_percent`
- Direction: Arduino → HA
- Unit: `%`
- Meaning:
  - calculated value from `soil_air`, `soil_water`, current raw value, and `soil_depth_mm`
  - air reference corresponds to `0 %`
  - water reference at `120 mm` corresponds to `100 %`
  - valid values are constrained to `0..100 %`
  - if `soil_depth_mm < 20`, the value should be unavailable/invalid

### Soil Moisture Raw Value
- Entity type: `sensor`
- Name: `soil_moisture_raw`
- Direction: Arduino → HA
- Unit: raw / analog
- Meaning:
  - current ADC-related raw value of the sensor
  - may also be published when percent calculation is invalid

### Fan RPM
- Entity type: `sensor`
- Name: `fan_rpm`
- Direction: Arduino → HA
- Unit: `rpm`

## Fault Entities

### Light Fault
- Entity type: `binary_sensor`
- Name: `light_fault`
- Direction: Arduino → HA
- Meaning:
  - fault in the light path, especially AD5263/I²C/readback related

### Fan Fault
- Entity type: `binary_sensor`
- Name: `fan_fault`
- Direction: Arduino → HA
- Meaning:
  - fan should effectively be running, but no tach pulses after grace period

### SHT Fault
- Entity type: `binary_sensor`
- Name: `sht_fault`
- Direction: Arduino → HA
- Meaning:
  - SHT3x communication or sensor status is problematic

### RTC Fault
- Entity type: `binary_sensor`
- Name: `rtc_fault`
- Direction: Arduino → HA
- Meaning:
  - DS3231 unavailable or time-base problem

### EEPROM Fault
- Entity type: `binary_sensor`
- Name: `eeprom_fault`
- Direction: Arduino → HA
- Meaning:
  - AT24C32 unavailable or persistence problem

### Light Fault Reason
- Entity type: `sensor` (text)
- Name: `light_fault_reason`
- Direction: Arduino → HA
- Meaning:
  - textual cause for `light_fault`
- Examples:
  - `ad5263_not_found`
  - `ad5263_write_failed`
  - `ad5263_readback_mismatch`

The detailed AD5263 readback and retry strategy is described in `MODULES.md`.

Not part of the model:

- `sensor_fault`
- `system_fault`
- `last_fault_code`

## Switches

### Fan Switch
- Entity type: `switch`
- Name: `fan`
- Direction: HA ↔ Arduino
- Meaning:
  - manual fan on/off

### Fan Auto Mode
- Entity type: `switch`
- Name: `fan_auto_mode`
- Direction: HA ↔ Arduino
- Meaning:
  - ON: SHT alert controls the fan
  - OFF: manual fan control

### Light Auto Mode
- Entity type: `switch`
- Name: `light_auto_mode`
- Direction: HA ↔ Arduino
- Meaning:
  - ON: Arduino schedule via DS3231 alarms active
  - OFF: HA control active

### Light Hard Power Off
- Entity type: `switch`
- Name: `light_hard_power_off`
- Direction: HA ↔ Arduino
- Meaning:
  - switches the relay immediately
  - dimmer state is retained internally

### Fallback Behavior
- Entity type: `switch`
- Name: `light_fallback_to_auto`
- Direction: HA ↔ Arduino
- Meaning:
  - ON: activate internal Arduino Auto Mode for the light during longer offline state
  - OFF: turn light off during longer offline state

## Light

### Grow Light
- Entity type: `light`
- Name: `grow_light`
- Direction: HA ↔ Arduino
- Functions:
  - ON/OFF
  - Brightness

### Important Rule
The Light entity is only fully effective when:

- `light_auto_mode = OFF`

When `light_auto_mode = ON`:
- brightness changes may optionally be treated as temporary correction
- HA schedule triggers are ignored
- on/off may be ignored or disabled

## Number Entities

### Temperature / Humidity Thresholds
- `temp_high_set`
- `temp_high_clear`
- `temp_low_set`
- `temp_low_clear`
- `hum_high_set`
- `hum_high_clear`
- `hum_low_set`
- `hum_low_clear`

Direction:
- HA ↔ Arduino

Persistence:
- yes, external AT24C32 EEPROM via JC_EEPROM

### Arduino Light Schedule
- `light_on_time_minutes`
- `light_off_time_minutes`
- `light_dim_minutes`

Direction:
- HA ↔ Arduino

Persistence:
- yes, external AT24C32 EEPROM via JC_EEPROM

Note:
- time format = minutes since midnight
- changes must also update the DS3231 alarm registers

### Soil Moisture Calibration
- `soil_air`
- `soil_water`
- `soil_depth_mm`

Direction:
- HA ↔ Arduino

Persistence:
- yes, external AT24C32 EEPROM via JC_EEPROM

Limits:

- `soil_air`: min 0, max 1000, step 1
- `soil_water`: min 0, max 1000, step 1
- `soil_depth_mm`: user-entered millimeter value with project-defined limits

Note:

- `soil_air` and `soil_water` use the expected real project range; air values are expected below approx. 900.
- The firmware may set separate hard internal ADC safety limits to `0..4095`.
- `soil_depth_mm` is an active correction parameter, not merely informational.
- Values below `20 mm` are invalid for percent calculation because the first physical sensor marking is at `20 mm`.
- Water reference is `120 mm`.

Conceptual percent calculation:

```text
SOIL_REFERENCE_DEPTH_MM = 120
depth_factor = soil_depth_mm / SOIL_REFERENCE_DEPTH_MM
percent = (soil_air - raw) / ((soil_air - soil_water) * depth_factor) * 100
```

Valid percent values are constrained to `0..100 %`. If `soil_depth_mm < 20`, `sensor.soil_moisture_percent` remains unavailable/invalid; `sensor.soil_moisture_raw` may continue to be published.

The linear depth correction is a deliberate approximation. A measurement series exists,
but the project currently accepts this model as sufficiently accurate.

### HA Dimming Request
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`

Direction:
- HA → Arduino

Persistence:
- no

Meaning:
- runtime/command parameters for an explicit HA dimming job
- become effective only when `start_ha_dim` is triggered
- resumption of a running HA dimming job after restart happens through the internally persisted resume state (not through the Number states themselves)

## Buttons

### Sync Time
- Entity type: `button`
- Name: `sync_time`
- Direction: HA → Arduino
- Effect:
  - trigger NTP sync
  - update RTC
  - rewrite DS3231 alarms

### Read Soil Raw Value
- Entity type: `button`
- Name: `read_soil_raw_value`
- Direction: HA → Arduino
- Effect:
  - immediately read and publish current raw value
  - intended for HA-guided calibration
  - HA then writes the published raw value to `number.soil_air` or `number.soil_water`

Separate buttons such as `capture_soil_air` or `capture_soil_water` are not part of the model.

### Start HA Dim
- Entity type: `button`
- Name: `start_ha_dim`
- Direction: HA → Arduino
- Effect:
  - starts a dimming request from the current actual brightness
  - target = `ha_dim_target_percent`
  - duration = `ha_dim_duration_minutes`

### Important Rule
`start_ha_dim` is only effective when:

- `light_auto_mode = OFF`

When `light_auto_mode = ON`, the command is ignored.

## Soil Moisture Calibration Workflow

The firmware does not manage a calibration assistant and does not contain an internal
calibration state machine. Home Assistant orchestrates the workflow with the
generic button `read_soil_raw_value` and the three persistent Number entities.

Air step:
- The user places the sensor dry in air.
- HA calls `button.read_soil_raw_value`.
- HA waits briefly until `sensor.soil_moisture_raw` is updated.
- HA writes this value to `number.soil_air`.

Water step:
- The user places the sensor in water at the reference depth of `120 mm`.
- HA calls `button.read_soil_raw_value`.
- HA waits briefly until `sensor.soil_moisture_raw` is updated.
- HA writes this value to `number.soil_water`.

Depth step:
- The user manually enters the actual insertion depth in the substrate in `number.soil_depth_mm`.
- The firmware actively uses this value for percent calculation.

Separate firmware buttons such as `capture_soil_air` or `capture_soil_water`
should not be added. The generic raw-read button plus HA scripts or
HA automations is sufficient and avoids unnecessary MQTT entities.

## State Recovery / Re-Publish

After startup or MQTT reconnect, the following should be actively published again:

- switch states
- light state
- number configuration values
- fault states including `light_fault_reason`
- current sensor values

Therefore separate diagnostic sensors for:

- actual light brightness
- light mode

are not strictly required.

## Optional Future Diagnostic Entities

Only if desired:

- active light control mode
- current AD5263 code or derived dimmer resistance
- fallback active
- last NTP sync time
- RTC alarm state

These are optional and not part of the mandatory scope.
