# Local RTC Light Schedule Runtime Test

This interim installed-system test validates a local DS3231-driven light schedule that continues while WiFi,
MQTT, or Home Assistant is unavailable. It is deliberately a separate Home Assistant device and does not
replace System Test 08 or System Test 09.

Sketch identity: `08a_LocalLightScheduleRuntimeTest v1.0.1`

## Purpose

- run two daily RTC alarm targets without a network dependency;
- synchronize the local RTC through non-blocking UDP NTP when WiFi is available;
- apply Europe/Vienna summer/winter transitions locally between NTP synchronizations;
- persist schedule, hard-power-off, DST, and alarm de-duplication state in a separate AT24C32 record;
- exercise non-flickering AD5263 ramps on the installed light;
- publish TSL2591 raw channels, its D9 line, and soil raw data once per minute; and
- keep OTA and Home Assistant diagnostics available without making the local schedule depend on them.

The implementation takes its non-blocking NTP request flow from the confirmed AutoTerra
`ha-ntp-rtc-alarms` hardware test, the TSL2591 setup from the production `LightSensor`, and the live AD5263
write/readback behavior from the accepted light system tests. It does not initialize SHT or fan control.

## Hardware And Safe State

The target is the installed Arduino Nano 33 IoT hardware documented by the project schematic.

| Function | Pin/address | Test behavior |
| --- | --- | --- |
| Fan switch | D2 | forced `LOW` before all other initialization and never changed |
| Light relay | D3 | opened before all other initialization |
| AD5263 `SHDN` | D4 | asserted before all other initialization |
| TSL2591 interrupt line | D9 | active-low line level polled; no ISR |
| DS3231 alarm | D10 / `EXTINT5` | `FALLING` ISR sets one flag only |
| Soil moisture | A0 | 12-bit raw input |
| TSL2591 | `0x29` | low gain, 100 ms integration |
| AD5263 | `0x2C` | both RDACs written and read back |
| AT24C32 | `0x57` | schedule record begins at byte 512 |
| DS3231 | `0x68` | local Europe/Vienna civil time and two alarms |

No I2C, sensor, MQTT, Home Assistant, or logging operation runs inside an ISR.

### Light transitions

- `>0 % -> >0 %`: only the RDAC values change; relay and `SHDN` stay active throughout the ramp.
- `0 % -> >0 %`: write and verify with relay open and `SHDN` asserted, release `SHDN`, then close the relay.
- `>0 % -> 0 %`: keep power active through the ramp, then open the relay and assert `SHDN` at the final step.
- any AD5263 write/readback fault: open the relay and assert `SHDN`.
- only exactly `0 %` is treated as normal electrical off; there is no provisional lower brightness clamp.

`Light Hard Power Off` is a persistent safety override. Enabling it opens the relay immediately, retains the
logical/RDAC target, and cancels the current ramp. RTC events continue to be recorded but cannot close the
relay. Releasing it first persists the cleared override, then derives, writes, and verifies the currently due
schedule target before power can return. A failed release write leaves the override on.

## Time Model

NTP supplies UTC only. The sketch uses a non-blocking 48-byte UDP request with a five-second response timeout,
ten-second retry interval, and 24-hour successful-sync interval. It validates leap state, response mode,
stratum, packet length, and supported epoch. The 32-bit NTP timestamp is decoded with era handling so the 2036
wrap remains valid through the DS3231-supported year 2099.

A successful correction of at most 60 seconds that does not cross a schedule target preserves an active ramp;
larger corrections reconcile the currently due target immediately.
The DS3231 stores local Europe/Vienna time. The sketch calculates the last Sundays in March and October and
performs the one-hour correction locally even without WiFi:

- a skipped spring `02:xx` target runs once immediately after the jump to `03:00`;
- an autumn `02:xx` target runs only in the first occurrence; and
- if both targets lie in the skipped spring hour, both steps are recorded in time order and the later target
  becomes effective.

On an ordinary boot with valid RTC and EEPROM data, the currently due target is applied immediately without
reconstructing an interrupted ramp. If RTC power was lost, the EEPROM record is invalid, or an uninitialized
DST state is read during an ambiguous transition hour, the light remains safe until NTP supplies valid time.
Pure boot self-tests check the 2026 EU transition dates, both `02:xx` interpretations, and NTP era decoding
without changing the real RTC.

## Persistence

The test owns a versioned, checksummed record at AT24C32 byte address `512`; records at address `0` are neither
read as this schema nor migrated. The record contains:

- both alarm hours, minutes, and target percentages;
- shared dim duration;
- hard-power-off state;
- current DST state and last applied DST transition date;
- last execution date for each alarm; and
- sequence and boot counters.

Writes use pre-read, changed-byte `update()`, and byte-identical readback. A failed boot read never causes a
speculative default write. A successfully read but uninitialized record receives defaults of `08:00/100 %`,
`20:00/0 %`, and 30 minutes. Equal alarm times are rejected and the last verified values are republished.

## Runtime Recovery And Watchdog

Version 1.0.1 replaces unchecked DS3231 hardware reads with a test-local checked transaction layer. Time
registers are validated as BCD and as a real 2000-2099 calendar value before a `DateTime` is created. Alarm
register writes are read back byte-for-byte. Alarm flags are read and verifiably cleared before acting. The
RTC line is serviced every 100 ms; a low line without an alarm flag is reported once per low episode, and a
line that remains low for 500 ms enters Recovery Lockout.

Any invalid RTC value, RTC transport failure, stuck-low alarm line, or failed critical bus recovery opens the
relay, asserts `SHDN`, cancels the ramp, and enables `Runtime Recovery Lockout`. Precedence is Recovery
Lockout, Hard Power Off, then schedule. After a watchdog reset, Wire, RTC, EEPROM, dimmer, and sensors are not
initialized. WiFi, HA, and OTA remain available. Turning Recovery Lockout off queues exactly one recovery
attempt outside the MQTT callback: Wire restarts at 100 kHz, a stuck bus receives at most nine SCL pulses and
a STOP, and RTC, AT24C32, and AD5263 must verify before schedule restore. A lost-power RTC waits for checked
NTP recovery after that manual attempt.

The SAMD21 watchdog is configured for 16 seconds and fed only after a complete main-loop pass. OTA uses a
storage proxy that feeds it while flash data is accepted. OTA start forces the light electrically off. A
watchdog reset enters the non-persistent Recovery Lockout and requires manual release. `Trigger Watchdog
Recovery Test` publishes its armed step before intentionally stopping watchdog feeds.

## Home Assistant Interface

The test uses one ArduinoHA connection, capacity 56, and exactly 56 entities under its own device:

- device ID: `grow_controller_test_local_light_schedule`
- device name: `Local RTC Light Schedule Test`
- data prefix: `smaeenhouse/test/local_light_schedule/ha`

The seven schedule numbers and `switch.light_hard_power_off` remain. Version 1.0.1 adds
`switch.runtime_recovery_lockout` and `button.trigger_watchdog_recovery_test`. New diagnostics are
`reset_cause`, `i2c_bus_status`, `i2c_recovery_count`, `mqtt_publish_error_count`, and
`rtc_spurious_irq_count`.

The combined sketch identity is published in `sensor.sketch_identity` on every HA connection. Uptime remains
unbounded `h:mm` text. Application states are sent one publication per 200 ms, with recovery and electrical
states first. At most one queued event follows three state publications. A failed event remains queued. Three
consecutive application publish failures close the MQTT transport for a clean reconnect.

`sensor.test_step` contains sequence, `source`, `target`, and `result`. The obsolete `test_step_index` entity
is removed and its retained discovery/state topics are cleared through the same ArduinoHA connection. There
is no direct diagnostic MQTT client. Minute TSL/D9/soil samples retain only the latest local snapshot for the
paced reconnect refresh. Invalid but successfully transported RTC data is a semantic fault and does not
increment the RTC I2C counter; a failed logical transport increments it once.
## Credentials And Compile Check

Copy `Credentials.example.h` to local `Credentials.h` and configure WiFi, MQTT, OTA, and NTP values. Never
commit `Credentials.h`.

```powershell
$env:SKETCH = "sketches/system-tests/08a_LocalLightScheduleRuntimeTest"
.\scripts\check-arduino.ps1
```

The repository check creates and removes a temporary credentials file only when no real local file exists.

## Installed-System Acceptance

Because v1.0.0 can become unreachable under the real lamp load, install v1.0.1 with the lamp load temporarily
disconnected. After HA availability is established, enable persistent Hard Power Off before reconnecting the
load.

1. Confirm identity `v1.0.1`, reset cause, self-test pass, fan off, relay open, `SHDN` asserted, and the new
   recovery diagnostics. Confirm that retained cleanup removes `test_step_index`.
2. Release Hard Power Off and confirm valid RTC BCD/calendar data, verified alarm registers, and the due
   target. An impossible date/time must never be published as valid.
3. Change all seven schedule values. Equal times must be rejected. A reconcile event must use source
   `config_reconcile` and must not be reported as an RTC alarm.
4. Trigger each real RTC alarm. Require the matching flag, source `rtc_alarm1` or `rtc_alarm2`, verified flag
   clear, persisted execution day, and expected ramp.
5. Repeat `0 -> >0`, `>0 -> >0`, and `>0 -> 0`. The middle ramp must not switch relay or `SHDN`.
6. Enable Recovery Lockout during a non-zero ramp. Output must turn off immediately and no I2C work may occur
   until one manual release attempt is requested.
7. Release Recovery Lockout. Require one bus-recovery event, verified RTC/EEPROM/AD5263, and schedule restore.
   A failed recovery must leave lockout on and the light off.
8. Press `Trigger Watchdog Recovery Test`. Require the armed event, watchdog reset cause after reboot, light
   off, HA/OTA reachable, and Recovery Lockout on. Release it manually and verify one recovery.
9. With Hard Power Off on, reconnect the lamp load and reboot by OTA. Persistent Hard Power Off must remain on;
   Recovery Lockout must reflect only runtime recovery state.
10. Disable WiFi across an alarm and multiple retry/module-reset cycles. The local schedule must continue,
    latency must remain at or below ten seconds, reconnect state output must be paced, and OTA must work.
11. Observe multiple minute samples and confirm TSL raw channels, D9, and soil raw update together.
12. Complete a 30-minute soak with both alarms, one WiFi interruption, and a final OTA. Reject I2C faults,
    duplicate alarms, non-deliberate watchdog resets, actuator deviations, or unexplained availability loss.
## Results And Notes For The Next Test

- Confirmation status: Reopened after the v1.0.0 alarm-trigger availability and OTA failure.
- Date / firmware revision: `08a_LocalLightScheduleRuntimeTest v1.0.1` implemented 2026-08-11.
- Required observations: all twelve v1.0.1 acceptance steps above.
- Anomalies or limitations: v1.0.0 became unavailable and published an impossible RTC value. Version 1.0.1 is the corrective build. Legal changes to EU DST rules require firmware maintenance; real transition-day
  behavior is represented initially by deterministic self-tests rather than waiting for a seasonal boundary.
- Safety notes to carry forward: only `0 %` is canonical normal off; non-zero live ramps must not cycle relay or
  `SHDN`; Hard Power Off always outranks the schedule.
- Entity or topic notes to carry forward: this test is a separate HA device and has no direct diagnostic MQTT
  client or SHT/fan entities.
