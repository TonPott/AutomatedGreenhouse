# System Test 09: Arduino Schedule and RTC Light

Status: ready for hardware validation

Sketch version: `1.0.1`

## Purpose

This OTA-capable test validates Arduino-controlled light scheduling through the two DS3231 alarms. It also
provides an unrestricted `0..100 %` AD5263 command range so the installed lamp's real dark-to-active and
effective full-output boundaries can be measured before production constraints are defined.

The sketch is derived only from the accepted
[`08_LightRelayManualHaControlTest`](../08_LightRelayManualHaControlTest/README.md). It preserves that test's
network, OTA, EEPROM/RTC, SHT alert/fan, soil, Home Assistant, safe relay, `SHDN`, AD5263 readback, and fault
behavior. Hardware-test sketches are not implementation sources.

Version `1.0.1` uses SHT ALERT on `A7` (`PB03` / `EXTINT3`) and validates the active board core mapping before attaching the ISR. This pin-only correction does not invalidate the inherited Test 08 light-control result.

No real Home Assistant export or measurement history belongs in the repository.

## Important Brightness Rule for This Test

There is deliberately no `LIGHT_MIN_ACTIVE_PERCENT` clamp in this sketch.

- `0 %` is the canonical hard-off target and always leaves the relay open.
- Every value from `1 %` through `100 %` is passed unchanged to the current two-channel AD5263 mapping.
- A non-zero command may leave the powered lamp dark during characterization. That observation identifies
  the physical dark band and is not rejected by the test firmware.
- The upward and downward sweeps determine the first reliably illuminated setting, the first effective
  full-output setting, hysteresis, and delayed response.
- Any later production lower/upper bounds remain compile-time installation configuration rather than Home
  Assistant entities.

## RTC and Schedule Behavior

The confirmed DS3231 SQW/INT connection remains on `D10` / `PA21` / `EXTINT5`.

- The ISR only sets `rtcAlarmPending`.
- RTC reads, alarm identity checks, clearing, rearming, Home Assistant publication, and light actions run in
  the normal loop.
- `Alarm1` uses `light_on_time_minutes` and `light_on_target_percent`.
- `Alarm2` uses `light_off_time_minutes` and `light_off_target_percent`.
- Times are local RTC minutes since midnight in the range `0..1439`.
- Each fired alarm is cleared and rearmed for the next matching local calendar day.
- `light_dim_minutes` applies to both scheduled transitions and accepts `0..1440` minutes.
- If both alarms are deliberately assigned the same minute, Alarm2 is evaluated after Alarm1 and therefore
  becomes the final scheduled request. Use distinct minutes for normal validation.

A schedule event starts a non-blocking linear percent ramp. Non-zero-to-non-zero steps write and verify the
AD5263 while relay and `SHDN` remain stable. A transition from or to `0 %`, startup, OTA, or a fault uses the
full safe sequence with relay open and `SHDN` asserted before the target is changed.

Switching `light_auto_mode` off or enabling hard power off cancels a running Arduino schedule job. Manual
Home Assistant light commands remain rejected while `light_auto_mode` is on. Hard power off remains a
separate safety control.

## Persistence Migration

Version `1.0.0` extends the inherited test record with:

- `lightOnTargetPercent`
- `lightOffTargetPercent`

The EEPROM record version changes from `1` to `2`. A valid version-1 record is migrated in place while
preserving its sequence, boot count, fan/light modes, schedule times, dim duration, SHT high thresholds, and
soil calibration. The new targets start at `100 %` and `0 %`. The migration increments boot count and sequence
once and verifies the resulting checksum/readback.

The manual light state and hard-power-off state remain volatile. OTA startup immediately opens the relay,
asserts `SHDN`, and cancels a running schedule job.

## Home Assistant Additions

The existing device identifier and MQTT data prefix remain unchanged so previously accepted entities and
history are retained. The ArduinoHA capacity is `72`.

New persistent numbers:

- `number.light_on_time_minutes`
- `number.light_off_time_minutes`
- `number.light_on_target_percent`
- `number.light_off_target_percent`
- `number.light_dim_minutes`

New test/history sensors:

- `sensor.light_schedule_event`
- `sensor.light_schedule_state`
- `sensor.light_schedule_progress_percent`
- `sensor.light_alarm1_next_epoch`
- `sensor.light_alarm2_next_epoch`

`Light Schedule Event` and the inherited `Light Test Step` use indexed, force-updated values. Each alarm,
schedule start, live dim step, safe transition step, failure, cancellation, and completion is therefore
visible in Home Assistant history while MQTT is connected. Expected/readback RDAC values, effective
brightness, relay state, `SHDN`, alarm counters, ISR count, next alarm epochs, and persistence counters are
published with the same run.

Diagnostic MQTT uses:

- `smaeenhouse/test/arduino_schedule_rtc_light/status`
- `smaeenhouse/test/arduino_schedule_rtc_light/event`

Direct MQTT actuator commands remain disabled.

## Build

From the repository root:

```powershell
$env:SKETCH = "sketches/system-tests/09_ArduinoScheduleRtcLightTest"
.\scripts\check-arduino.ps1
```

The compile script may create `Credentials.h` from `Credentials.example.h` only when no real file exists and
must remove that temporary file after the check.

## Test Procedure

### 1. Installation and inherited state

1. Compile and install version `1.0.1` by OTA.
2. Confirm `Sketch Identity` reports `09_ArduinoScheduleRtcLightTest v1.0.1`.
3. Confirm relay open and `SHDN` asserted during OTA/startup before any possible relay close.
4. Confirm EEPROM checksum/readback, boot count, and sequence are valid and the version-1 record was migrated
   without losing the previously accepted configuration.
5. Confirm RTC, SHT, fan, soil, network, OTA, and AD5263 entities republish after MQTT connection.

Previously accepted Test 08 manual safety points do not need to be repeated unless a regression is observed.

### 2. Unrestricted real-lamp sweep

1. Set `light_auto_mode = OFF` and clear hard power off.
2. Set brightness to `0 %` and confirm the relay is open.
3. Increase brightness from `1 %` upward in one-percent steps. Allow the physical lamp response to settle at
   every step near the first visible output.
4. Record the first setting that illuminates reliably, not merely a transient flash.
5. Continue upward until further percent increases produce no meaningful additional lamp output. Record the
   first effective full-output setting.
6. Sweep downward from `100 %` to `1 %` and record the corresponding full-output and dark boundaries.
7. Note any turn-on/turn-off hysteresis, delayed response, flicker, or unstable interval.
8. Return to `0 %` and confirm immediate relay-open hard off.

Do not add a firmware clamp during the run. The measured boundaries are reviewed before configuration values
are chosen.

### 3. Alarm configuration and immediate schedule action

1. Set `light_dim_minutes = 0` for immediate target transitions.
2. Set distinct Alarm1 and Alarm2 times to the next two local RTC minutes.
3. Set Alarm1 target to `100 %` and Alarm2 target to `0 %`.
4. Enable `light_auto_mode`.
5. Confirm each RTC interrupt, alarm counter, clear, next-day rearm, schedule event, correct target, RDAC
   expected/readback, relay, `SHDN`, and completion in Home Assistant history.
6. Confirm manual HA on/off and brightness commands are rejected while auto mode is active.

### 4. Non-default alarm targets and scheduled dimming

1. Choose two safe, visibly distinct non-zero targets from the measured range, including a non-zero Alarm2
   target.
2. Set `light_dim_minutes = 1` and schedule the alarms far enough apart for each ramp to complete.
3. Confirm every indexed percent step, continuous non-zero relay/`SHDN` state, RDAC readback, progress, and
   completion.
4. During another ramp, switch `light_auto_mode = OFF` and confirm the schedule job stops without activating a
   second control source.
5. Restore auto mode and safe schedule values.

### 5. Persistence, WiFi, and OTA

1. Store non-default times, targets, and dim duration.
2. Disable WiFi long enough to exercise the established reconnect and NINA-reset path, then restore it.
3. Confirm local RTC scheduling continues and all current HA states republish after reconnect.
4. Perform an OTA update and confirm safe outputs during restart.
5. Confirm schedule configuration, alarm targets, auto mode, boot count, and sequence are restored; the manual
   light state and hard-power-off state are reset according to the inherited volatile-state rules.
6. Complete a soak covering at least one event from each alarm and check for unexpected RTC, EEPROM, AD5263,
   network, OTA, SHT, fan, or soil faults.

## Acceptance Criteria

The test passes when:

- the physical lower active and effective full-output boundaries have been recorded in both directions;
- the firmware has accepted and reported the complete `0..100 %` test range without hidden normalization;
- Alarm1 and Alarm2 independently apply their persisted targets only in Arduino auto mode;
- both alarms are cleared and rearmed for the next day through loop code after the minimal ISR flag;
- non-zero scheduled dim steps do not cycle the relay or `SHDN`;
- every connected-run alarm and dim step is visible in Home Assistant history;
- `0 %`, startup, OTA, and faults retain the verified safe relay/`SHDN` behavior;
- schedule values survive reconnect and OTA migration without continuous EEPROM writes; and
- no previously accepted system-test behavior regresses.

After acceptance, document the measured boundaries and decide how the compile-time production mapping should
use them. Do not begin Test 10 with an assumed `5 %` threshold.
## Results And Notes For The Next Test

Hardware result: pending.

Do not mark Test 09 complete or begin Test 10 until the upward/downward lamp boundaries, both RTC alarm
targets, non-zero scheduled ramp behavior, persistence/OTA recovery, WiFi recovery, and soak have been
confirmed from the installed system. Record only summarized results here; keep the Home Assistant export
outside the repository.