# Light Relay And Manual HA Control Test

This maintenance revision also uses the shared retained-entity manifest for the `Grow Controller Tests` device. On connection it removes discovery for every known Test 02-09 entity not active in this sketch and clears orphaned states from the other known shared data prefix. Cleanup uses the existing ArduinoHA connection and publishes at most one retained deletion per loop pass.

Sketch: `08_LightRelayManualHaControlTest.ino`

Version: `1.0.4`

## Status

The light-control acceptance remains complete. Version `1.0.4` is a corrective infrastructure revision and is **Reopened** only for focused SHT, EEPROM, A7, availability, and final-OTA confirmation. Previously accepted light-control points do not need to be repeated.

## Purpose

This OTA-capable system test adds controlled light-relay operation and the production Home Assistant manual-light controls to the confirmed system-test runtime. It proves that the relay cannot close before the AD5263 target has been written, read back, and released from shutdown. It also proves the inverse shutdown order and the always-available hard power-off override.

Arduino schedule actuation is deliberately not implemented here. RTC alarms continue to be configured, handled, counted, and cleared through the inherited runtime, but they cannot operate the light in Test 08. Schedule-driven light behavior belongs to Test 09.

## System-Test Lineage

The sketch is derived only from `07_Ad5263DimmerSafeReadbackTest`. It preserves the established WiFi/OTA recovery, EEPROM/RTC, SHT alert, fan, soil, sketch-identity, AD5263 mapping, exact readback, and light-fault behavior. No hardware-test sketch is used as a source.

The test-local AD5263 controller remains a snapshot. It does not change production `SHTa`, production `LightController`, or any production source file.

## Entry Condition

Satisfied on 2026-07-29. The accepted Test 07 soak confirmed:

- no unexpected relay closure
- no persistent light fault or readback drift
- no continuous EEPROM writes
- no OTA poll-gap regression
- no regression in the inherited network, RTC, SHT, fan, or soil behavior

Test 08 may now be installed and evaluated under the safety procedure below.

## Hardware And Safety

The project schematic is the primary wiring reference. The light path under test is:

```text
Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-
```

Relevant connections:

- AD5263 at `0x2C`, with `DIS=1`
- AD5263 `SHDN` on `D4`, with the documented external 10 kOhm pull-down
- 3.3 V light power relay on `D3`
- all sensors, fan, DS3231, and AT24C32 paths inherited from Test 07

Before Serial, I2C, storage, sensors, or networking start, the sketch opens the relay, asserts `SHDN`, and commands the fan off. After I2C starts, it writes and exactly verifies the 0% RDAC target while the relay remains open and `SHDN` remains asserted.

An AD5263 presence, write, or readback fault immediately opens the relay and asserts `SHDN`. The OTA start callback also opens the relay before asserting `SHDN`; the next boot reconstructs the same safe 0% state.

## Light Control Semantics

### `switch.light_auto_mode`

- `ON`: manual `light.grow_light` state and brightness commands are rejected and the actual state is republished.
- `OFF`: Home Assistant owns manual light state and brightness.
- The mode is stored in the inherited external EEPROM record and survives reset and OTA.
- RTC alarm events never actuate the light in this test, including while auto mode is on.

### `light.grow_light`

With auto mode off:

- a non-zero brightness request performs the full safe on sequence and then closes the relay
- brightness `0` performs the full safe off sequence
- an ON request restores the last non-zero RAM brightness, defaulting to 100% after boot
- an OFF request leaves the last non-zero value available for the next ON request
- manual light state, brightness, and last-non-zero brightness are test-local volatile state and restart safely at OFF

For deliberate observability, a light request advances through a non-blocking state machine with a 250 ms minimum spacing between hardware stages. The short staged transition is test behavior, not a proposed production UI latency.

### `switch.light_hard_power_off`

- The switch is accepted in both auto and HA mode.
- Turning it on preempts any running light action and opens the relay immediately in normal loop context.
- It retains the current RDAC target and internal brightness; it does not rewrite the dimmer merely to power the lamp off.
- Releasing it with a non-zero target verifies the retained RDAC bytes, releases `SHDN`, and only then closes the relay.
- Releasing it with a zero target leaves the relay open and `SHDN` asserted.

## Enforced Hardware Order

Normal ON or non-zero brightness:

1. accept the HA request only in HA mode
2. open the relay
3. assert `SHDN`
4. write W2 and W1
5. read back and exactly verify W2 and W1
6. release `SHDN`
7. close the relay only if no light fault and hard power off is inactive

Normal OFF:

1. open the relay
2. assert `SHDN`
3. write and verify the 0% target

Hard power off bypasses the normal staged action only in the safe direction: it cancels the action and opens the relay immediately. All I2C, MQTT, and Home Assistant work remains in normal loop context. Existing ISRs only set flags or count tach pulses.

## Home Assistant Entities

The ArduinoHA capacity remains `64`. Test 08 preserves its 60 existing entities and adds one sequenced infrastructure-history entity, for 61 total:

- `light.grow_light`
- `switch.light_auto_mode`
- `switch.light_hard_power_off`
- `sensor.test_event`

The three light entities remain the only production controls added by this test.

The seven Test 07 AD5263 history entities remain available. Their friendly names are generalized for Test 08 where appropriate:

- `sensor.ad5263_test_step` -> `Light Test Step`
- `sensor.ad5263_step_index` -> `Light Step Index`
- `sensor.ad5263_brightness_percent` -> `Light Effective Brightness`
- expected and readback W2/W1 sensors retain their existing IDs and meaning

`binary_sensor.persistence_rtc_relay_safe` is presented as `Light Relay Open`, and `binary_sensor.persistence_rtc_shdn_safe` as `Light SHDN Asserted`. An `off` value is expected while the light is validly operating.

`sensor.sketch_identity` publishes `08_LightRelayManualHaControlTest v1.0.4` once per MCU boot. Separate sketch-name and sketch-version entities are not created.

## HA Step History

Home Assistant is the authoritative history source; USB Serial is optional. Every accepted, rejected, completed, or failed light action increments the step index and immediately publishes a complete snapshot. Numeric values are force-published and `sensor.ad5263_test_step` is published last as the commit marker with value `<index>:<step>`.

A normal non-zero request must show, in order:

1. `light_brightness_requested`
2. `light_relay_opened`
3. `light_shdn_asserted`
4. `light_target_write_readback_ok`
5. `light_shdn_released_relay_open`
6. `light_relay_closed_after_verified_target`
7. `light_action_complete`

A normal OFF request must show the request, relay-open, shutdown-asserted, verified-zero, and completion steps. Hard power off must show `light_hard_off_immediate_relay_open_target_retained`. A manual light command in auto mode must show `light_manual_command_rejected_auto_mode` without changing the relay or target.

No periodic 30-second interval is allowed to suppress these action snapshots. A missing index or a relay-closed snapshot before target verification and shutdown release fails the test.

## HA Infrastructure Diagnostics

`sensor.test_event` publishes sequenced boot, EEPROM, SHT, recovery, reconnect, and cleanup steps. Up to 24 pending events are buffered and at most one is published per loop pass. Light-action history continues to use the complete indexed `sensor.ad5263_test_step` snapshot described above.

Test 08 maintains only the ArduinoHA MQTT connection. The old direct status, event, and command topics are not subscribed or republished; their retained values are cleared through the existing ArduinoHA connection. MQTT Explorer is not required for acceptance.

## Credentials And Compile Check

Copy `Credentials.example.h` to the ignored local `Credentials.h` for upload. Never commit real credentials.

```powershell
$env:SKETCH = "sketches/system-tests/08_LightRelayManualHaControlTest"
.\scripts\check-arduino.ps1
```

On Linux or CI:

```bash
SKETCH=sketches/system-tests/08_LightRelayManualHaControlTest ./scripts/check-arduino.sh
```

The compile script may create `Credentials.h` from the example only when none exists and must remove that temporary file afterward.

## Installed-System Acceptance Procedure

1. Confirm the recorded Test 07 completion and accepted soak before uploading this sketch. This entry condition was satisfied on 2026-07-29.
2. Compile Test 08. Confirm temporary credential cleanup and statically confirm that ISRs only set flags or count pulses.
3. Install by OTA. Confirm the relay opens at OTA start and remains open through reboot, `SHDN` is asserted, the verified target is 0%, and no light fault is present.
4. Confirm all 61 entities, the combined sketch identity, the boot-safe step, and all inherited sensor, fan, RTC, EEPROM, and network behavior.
5. Leave `light_auto_mode=ON`. Send light ON, OFF, and brightness commands. Each must be rejected in HA history without relay closure.
6. Set `light_auto_mode=OFF`. Confirm the EEPROM sequence advances once and the mode survives a later restart.
7. Request 25%, 50%, 75%, and 100% separately. For every request, confirm the complete indexed sequence, exact expected/readback bytes, `SHDN` release before relay closure, visible lamp response, and no light fault.
8. Turn the light OFF and ON. Confirm relay-open-before-shutdown on OFF and last-non-zero brightness restoration on ON.
9. While the light is on, enable hard power off. Confirm immediate relay opening, unchanged brightness and expected/readback target, and no wait for an in-progress action. Release hard power off and confirm target re-verification, `SHDN` release, then relay closure.
10. Repeat hard power off with auto mode on. Confirm it remains available while normal light commands remain rejected.
11. With auto mode off and the light stable, wait for both inherited RTC alarm counters to advance. Confirm neither alarm changes brightness, `SHDN`, or relay state.
12. During valid light operation, disable WiFi through at least three reconnect timeouts and one NINA reset. Confirm local light, fan, and sensor behavior continues, uptime does not restart, counters rise, and HA states are completely republished after reconnect.
13. Start an OTA update while the light is on. Confirm the relay opens before `SHDN` is asserted and the reboot returns to safe OFF with 0% verified before network startup. Confirm persisted auto mode and inherited boot/sequence behavior.
14. Run a two-hour soak in a documented stable light state. Reject unexplained relay or `SHDN` changes, readback drift, light faults, continuous EEPROM writes, OTA poll gaps, or regressions in inherited behavior.

Stop immediately on a light fault, unexpected relay closure, wrong lamp response, abnormal sound or smell, or any mismatch between requested brightness and the documented RDAC readback.

### Version 1.0.1 Focused Retest

The installed `1.0.0` run completed steps 3 through 6 and 8 through 14. They remained accepted after the scale-only correction and were not repeated.

The focused `1.0.1` Home Assistant history run confirmed brightness commands above the former apparent `39 %` ceiling, exact requested/effective values and readbacks through the `100 %` endpoint, complete indexed safe action sequences, and no light fault. Test 08 is complete and Test 09 may begin.

### Version 1.0.4 Focused Infrastructure Retest

Version `1.0.4` carries the corrected Test 05 SHT command spacing, controlled limit-pause/restart behavior, verified EEPROM transactions, active-high A7 alert semantics, and single-MQTT-connection event reporting into Test 08.

1. Compile Test 08 and verify the minimal SHT, RTC, and tach ISR bodies.
2. Install by OTA. Require identity `08_LightRelayManualHaControlTest v1.0.4`, all 61 entities, the complete queued boot sequence, `SHT Fault=off`, `EEPROM Fault=off`, and the first SHT sample only after the regular two-second interval.
3. Apply one valid SHT threshold change and restore the safe value. Require stop, write, readback, and restart steps, continued measurements, and no false SHT fault during the controlled pause.
4. Toggle `Light Auto Mode` once and restore the intended value. Require EEPROM pre-read, write-or-skip, byte-identical verify, and no EEPROM fault. The already accepted light actuation sequence does not need to be repeated.
5. If Test 05 has not already accepted the A7 path, produce one controlled alert assertion and require one active-high rising edge. Otherwise carry that hardware result forward.
6. Run beyond the earlier approximately 770-second availability failure point, then perform a final OTA. Require monotonic uptime until OTA, no unexplained SHT/EEPROM/availability failure, and complete HA state republish after reboot.

After these focused checks pass, close the maintenance revision without repeating the accepted brightness, hard-power-off, WiFi-outage, RTC-neutrality, or two-hour light soak points.

## Known Limitations

- The action-state spacing intentionally favors remote sequence auditability over final production responsiveness.
- The sketch validates manual HA light control only. Arduino schedule actuation and dim jobs remain for later tests.
- Manual brightness and hard-power state are volatile in this test; only `light_auto_mode` uses the inherited persistent record.
- Digital readback still does not independently measure effective resistance at the driver terminals.
- The accepted light-control result used the historical D7 SHT polling fallback. Version `1.0.4` uses the active-high `A7` / `EXTINT3` path; focused interrupt acceptance remains pending unless carried forward from Test 05.
- The installed `1.0.0` run recorded one OTA poll-gap violation shortly after startup; the counter did not increase during the subsequent extended soak. This remains a non-blocking startup observation and is not part of the scale-only retest.

## Results And Notes For The Next Test

- Confirmation status: Light behavior complete; version `1.0.4` infrastructure correction reopened for the focused checks above.
- Date / firmware revision: Version `1.0.0` installed run and version `1.0.1` focused brightness-scale confirmation accepted on 2026-07-30; corrective version `1.0.4` implemented on 2026-08-10.
- Confirmed observations: Home Assistant recorded commanded and effective brightness values above `39 %` through `100 %`, with matching AD5263 readbacks, complete indexed safe sequences, and no light fault. All other installed Test 08 observations remain accepted from version `1.0.0`.
- Anomalies or limitations: Version `1.0.0` omitted ArduinoHA's `100` brightness scale. HA therefore supplied `0..255` callback values, the test clamped values above `100`, and the HA entity appeared limited to approximately `39 %`. The AD5263 still reached and exactly read back its full target. Auto-mode activation intentionally retains the current light state until the next valid command or schedule event. One startup-only OTA gap was recorded without any later soak increase.
- Safety notes to carry forward: Never close the relay before exact RDAC verification and `SHDN` release. Always open the relay before asserting `SHDN`. Hard power off must preempt in the safe direction in every mode.
- Entity or topic notes to carry forward: Preserve the device ID/data prefix, the single combined sketch identity, all 60 prior entities, the new `sensor.test_event` history entity, and complete indexed HA action history. Do not restore the obsolete direct Test 08 status/event/command topics or a second MQTT client.
