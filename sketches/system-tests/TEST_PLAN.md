# System Test Plan

This plan defines the ordered system-test path from the completed OTA smoke test toward full project-requirement validation. It is intentionally separate from individual test READMEs: this file describes the sequence and dependency structure, while each sketch folder documents its own behavior, safe states, observations, results, and notes for the next test.

## Planning Principles

- `sketches/system-tests/00_OtaSmokeTest` is the completed baseline and is not repeated here as open work.
- Create one new sketch at a time. Do not normally create the next sketch until the previous sketch's required functions have been confirmed on the installed system. If the user explicitly requests early preparation during a final soak, document the unmet entry condition and prohibit installation until the predecessor is accepted.
- Each new sketch must preserve the confirmed runtime behavior from the previous sketch unless a later README explicitly documents a deliberate replacement.
- After the OTA smoke, safe-output, I2C, and SHT hardware baselines, long-run system tests should publish a focused set of production-relevant Home Assistant entities through the `Grow Controller Tests` device so Home Assistant can retain useful history graphs during multi-day runs. Avoid a parallel diagnostic MQTT client or redundant direct status/event topics when sequenced Home Assistant history provides the required evidence.
- Every sketch that touches actuators must put all connected peripherals into safe states during boot before network services start. Safe states include, at minimum: relay open, fan off, AD5263 shutdown or a documented safe dimmer setting, and no unintended I2C activity from ISRs. Outputs that are not part of the current test must not have entities, commands, callbacks, or local code paths that can change them after setup.
- Test READMEs must include a `Results And Notes For The Next Test` section. That section records confirmation status, anomalies, safe operating limits, and carry-forward assumptions that the next sketch must respect.
- Test sketches should publish production-equivalent HA entities when those values are useful for validating the feature under test or for long-run trend review. Every HA test removes known obsolete retained discovery and state topics through its existing ArduinoHA connection. Tests 02-09 keep the shared `Grow Controller Tests` manifest synchronized and delete at most one retained topic per loop pass; separate-device tests clean only their own device ID. Other observations remain under the separated `smaeenhouse/test/<test_id>/...` MQTT namespace.
- Real measurement histories, HA exports, and raw cabinet datasets are sensitive. Keep captures in ignored local paths and document only summarized or anonymized results.
- Every future system-test sketch defines a stable sketch name and explicit version. Publish them together only through `sensor.sketch_identity` with value `<sketch name> v<version>` once per MCU boot. Do not create separate sketch-name or sketch-version entities or a redundant direct `boot_identity` topic. Retry failed publications, do not duplicate successful identity publications on same-boot reconnects, and never publish from an ISR.
- Installed-system acceptance must not depend on a direct USB Serial connection. Serial remains optional diagnostics. When a test needs a time series or multi-step result review, publish dedicated Home Assistant history entities immediately for every step; periodic summary intervals and MQTT Explorer output are not sufficient substitutes.
- Every active test-local SHT ALERT definition uses `A7` (`PB03` / `EXTINT3`). Before attaching the ISR, validate the active Nano 33 IoT core descriptor instead of treating the numeric result of `digitalPinToInterrupt()` as a signed availability test.

## Required Per-Sketch README Additions

Each new sketch README must include the standard per-test documentation from `sketches/system-tests/README.md` plus this explicit section:

```text
## Results And Notes For The Next Test

- Confirmation status:
- Date / firmware revision:
- Required observations:
- Anomalies or limitations:
- Safety notes to carry forward:
- Entity or topic notes to carry forward:
```

If the test is not yet run, leave this section with `Not run yet` and the exact observations that must be filled in before the next sketch is created.

## Ordered Test Sequence

### 0. OTA Smoke Baseline - Complete

Existing sketch: `sketches/system-tests/00_OtaSmokeTest`

Confirmed scope:

- WiFi connection and indefinite reconnect attempts
- OTA availability independent of MQTT and Home Assistant
- regular OTA polling without long blocking delays
- MQTT/ArduinoHA uptime publication through the dedicated test device
- reboot and continued OTA availability after upload

Carry-forward rule: every later OTA-capable system test keeps OTA polling early and regular, keeps WiFi reconnect non-blocking, and must not make OTA depend on MQTT, Home Assistant, sensors, actuator jobs, or command loops. Because no standalone mode exists yet, stable WiFi recovery has priority over all later feature validation. Every sketch must retry indefinitely, reinitialize the NINA interface after repeated connection timeouts, expose recovery counters, and pass a controlled outage/recovery check before its feature result is accepted.

### 1. Safe Installed-System Baseline

Purpose: prove that a sketch can boot with the full installed peripheral set connected and immediately place all outputs in safe states before adding bus or actuator behavior.

Required behavior:

- Initialize all known output pins to safe states as early as possible.
- Keep the light relay open.
- Keep the fan switch off.
- Keep AD5263 `SHDN` asserted or otherwise document the verified safe dimmer state.
- Configure interrupt-capable input pins without doing I2C, MQTT, HA, or sensor reads in ISRs.
- Preserve OTA smoke-test network behavior and MQTT test status output.
- Publish no production HA entities unless a status entity is explicitly needed for the final firmware model; prefer direct MQTT test status.

Exit criteria before creating the next sketch:

- Safe states are observed at boot, during WiFi reconnect, during MQTT outage, and during OTA upload/reboot.
- No actuator moves unexpectedly with the complete installed wiring connected.
- The README records any inverted relay/fan behavior or pin-level surprise that later tests must account for.

### 2. I2C Inventory And Passive Sensor Baseline - Reopened

Purpose: identify device-specific versus bus-wide I2C failures while every actuator remains safe.

Revision `02_I2cPassiveBaseline v1.2.4` requirements:

- Probe only the replacement SHT31 at `0x44`, DS3231 `0x68`, AT24C32 `0x57`, AD5263 `0x2C`, and TSL25911 `0x29`; do not initialize devices, access registers, clear interrupt sources, or electrically recover the bus.
- Preserve the verified `v1.1.2` WiFi-to-ArduinoHA reinitialization behavior and use only the existing ArduinoHA/MQTT connection.
- Enable the SAMD input buffers on SDA and SCL after `Wire.begin()` without replacing the pins' peripheral multiplexing. Sample both lines before each scan.
- If either bus line is low, publish `bus_stuck`, increment its counter, mark every device unavailable and RTC/AD5263 as `not_probed`, skip every Wire call without increasing device error counters, and continue servicing HA and OTA.
- Publish a unique scan/device/address `started` phase immediately before each probe and its Wire code/meaning immediately after return. Complete successful scans with the existing sequenced test step.
- Publish the boot reset cause as read-only diagnostics, but do not arm a SAMD hardware watchdog in this passive revision.
- Pass the standard `InternalStorage` object directly to ArduinoOTA. Do not wrap or modify the non-returning flash-copy path.
- Block every I2C scan until MQTT has published the boot evidence; after that, retain the normal 30-second scan interval. Omit the unnecessary `WiFi.disconnect()` command before the first join.
- Poll A7 and the RTC alarm line without attaching SHT, RTC, or tach ISRs.

Acceptance starts with a complete controller/I2C power-off before connecting the replacement sensor; hot-plugging is excluded. Require ten clean scans, then at least four hours with the lamp physically disconnected from mains, followed by a controlled WiFi outage and final OTA. The run requires complete probe histories, real high SDA/SCL readings, no new NACKs or bus-stuck scans, stable HA/OTA, and unchanged safe outputs. If a probe stops returning, preserve the last `started` phase; automatic reset recovery is not claimed by this passive revision.

Historical result: the original baseline remains documented as accepted with the former SHT31 at `0x45`. Revision `v1.1.1` exposed and `v1.1.2` corrected a WiFi-to-MQTT lifecycle defect. A later `v1.1.2` run stopped both HA and OTA between scans 82 and 83. Revision `v1.2.0` exposed early-boot NINA/watchdog ordering; `v1.2.1` restored boot but three OTA apply attempts ended in watchdog resets, and two `v1.2.2` attempts still ended in watchdog resets despite its pre-apply disable. Revision `v1.2.3` restored stable HA/OTA service, but its SHT-absent/lamp-disconnected run reached persistent SDA-low after 102 complete scans and then safely skipped 424 scans. Revision `v1.2.4` is **Reopened** for the replacement `0x44` sensor and corrected bus-stuck availability semantics. Suite-wide address propagation remains paused until this run succeeds; Test 03 then validates active SHT transactions before higher tests resume.

### 2a. SHT Hardware Baseline - Reopened

Purpose: validate the installed SHT31 address, periodic measurement path, alert-limit transactions, recovery semantics, and active-high A7 monitoring before SHT behavior is reused by a fan test.

Revision `03_ShtHardwareBaseline v1.2.1` requirements:

- Configure A7 as `INPUT` with `RISING`; the ISR only sets a flag.
- Initialize through probe, Break, at least 1 ms guard, reset, idle status/limit capture, status clear, and periodic start.
- Send only `Fetch Data` in periodic mode. Stop periodic measurement before status, limit, round-trip, or recovery commands, then make exactly one restart attempt.
- Run unchanged byte-identical limit round trips only on request; do not perform periodic background limit reads or immediate rollback writes.
- Publish operation-specific errors and decoded Sensirion codes. An isolated fetch error remains historical evidence without asserting the binary fault; three consecutive fetch errors or any failed initialization/stop/limit/readback/restart path assert it.
- Clear the current fault only after complete verified recovery plus a valid measurement.

Acceptance requires five initial samples, three round trips at least 30 seconds apart, a 30-minute soak without unexplained NACKs/short reads, and final OTA. Historical stable `0x45` measurements and the isolated `268`/startup `527` errors remain recorded as motivation for the revision.

### 3. Persistence And RTC Alarm Configuration Test - Reopened

Purpose: isolate EEPROM transport, transfer, and record validity while retaining the accepted RTC alarm logic as a control channel.

Revision `04_PersistenceRtcBaseline v1.2.1` requirements:

- Remove the unused SHT interrupt and keep RTC evaluation in the loop behind a flag-only ISR.
- Publish separate transport, last-transfer, record-validity, recovery, and current-fault states with transaction phase, raw code, and sequenced test steps.
- Use full pre-read, comparison, contiguous changed-range writes split at 32-byte page boundaries, checksum/validity data last, and a full byte-identical readback.
- Initialize defaults only for a completely readable all-`0xFF` record. Never default-write after a transport/read failure or over a readable non-empty corrupt record.
- Treat the first probe/read transport failure as `DEGRADED` and retry read-only after 10 seconds; confirm a transport fault on a second independent failure. Write/readback/corrupt-record failures fault immediately.
- Preserve the last verified RAM state and reset HA commands to it after a failed write.

Acceptance requires successful boot validation, an unchanged verify skip, at least five verified `fan_auto_mode` changes with range/byte evidence, 30 minutes without further writes, and final OTA persistence validation.

Historical results from 2026-07-20 and the focused v1.1.0 acceptance on 2026-08-10 remain valid for their tested implementation. Revision `v1.2.1` is nevertheless **Reopened** because later combined tests repeatedly reported EEPROM faults while RTC and AD5263 stayed healthy.

### 4. SHT Alert And Fan Closed-Loop Test - Reopened

Existing sketch: `sketches/system-tests/05_ShtAlertFanClosedLoopTest`

Purpose: validate the required fan behavior driven exclusively by SHT alert logic.

Required behavior:

- Preserve OTA, safe-state, I2C, RTC, and persistence behavior.
- Configure SHT thresholds from persisted or HA-provided values and write them to the sensor at startup.
- Version `1.1.4` uses candidate pin `A7` / `EXTINT3`, verifies the installed core's actual pin descriptor, configures the SHT31 push-pull active-high signal as `INPUT`, and registers `attachInterrupt(digitalPinToInterrupt(A7), ..., RISING)` before SHT initialization. The ISR only sets a flag; a missing interrupt mapping fails the focused validation instead of being accepted as polling behavior.
- SHT interrupt edges are represented by a loop-consumed counter at the normal HA interval; only the alert-line state needs a change-driven update. Neither path publishes from the ISR.
- Use one ArduinoHA MQTT connection. Expose sequenced test steps through the existing `sensor.test_event` ID, buffer at most 24 pending steps, publish at most one per loop, and expose monotonic SHT measurement/status error counters instead of maintaining duplicate direct diagnostic topics.
- Decode SHT alert status in the main loop and use it as the only automatic fan decision source.
- Support manual fan switch and fan auto-mode semantics through production-relevant HA entities.
- Measure fan RPM only when the fan should be running, using the documented 2-pulse-per-revolution tach behavior and inverted 2N3904-conditioned signal.
- Set `fan_fault` when the fan should run but tach pulses are absent after the documented grace period.

Exit criteria:

- Fan remains off in all idle and boot safe states.
- Manual and auto fan modes are separated correctly.
- FanController does not evaluate temperature or humidity thresholds internally.
- RPM and fault behavior are plausible on the installed fan.

Historical result: accepted on 2026-07-21 for the historical D7/polling hardware revision. Manual and automatic fan control, separate temperature/humidity high cycles, fan-neutral low tracking, threshold feedback, stable post-start RPM behavior, and functional tach-fault recovery passed. The broken fan explains the latest tach data and is not treated as firmware evidence. Accepted functional steps do not need repetition. Never block the fan.

Corrective version `1.1.5` is implemented and remains **Reopened** pending hardware acceptance. It retains the previously implemented spaced SHT and byte-wise verified EEPROM flows, delayed 30-second SHT recovery, combined identity cleanup, and focused active-high A7 validation with both fan switches off. These are not the newly reopened Test 03 v1.2.1 and Test 04 v1.2.1 routines; Test 05 remains unchanged until those focused revisions pass. Its existing acceptance still requires two distinct A7 rising edges, verified fan-auto `ON -> OFF` persistence without actuation, a 20-minute run beyond the earlier failure point, and a final OTA.

### 5. Soil Moisture And Calibration Entity Test - Complete

Existing sketch: `sketches/system-tests/06_SoilMoistureCalibrationTest`

Purpose: validate soil raw/percent publishing and HA-controlled calibration without adding a firmware calibration assistant.

Required behavior:

- Preserve all previous behavior.
- Read the SEN0308 analog value periodically and on `button.read_soil_raw_value`.
- Publish `soil_moisture_raw` and valid `soil_moisture_percent` according to `soil_air`, `soil_water`, and `soil_depth_mm`.
- Treat `soil_depth_mm < 20` as invalid for percent calculation while continuing raw publication.
- Expose only the production calibration entities: `soil_air`, `soil_water`, `soil_depth_mm`, and `read_soil_raw_value`.
- Do not add `capture_soil_air`, `capture_soil_water`, or an internal calibration state machine.

Exit criteria:

- HA can orchestrate air/water/depth calibration using only the documented entities.
- Persisted calibration values survive restart.
- Invalid-depth behavior does not publish misleading percent values.

Confirmed result: accepted on 2026-07-28 with one non-blocking deviation. The installed sensor returned appropriate values at multiple insertion depths, percentage output remained within `0..100 %`, the raw-read button sampled immediately with the following periodic sample approximately 10 seconds later, calibration including an EEPROM write succeeded, and WiFi reconnected while local functions continued. The normal HA entity range prevented selecting `0 mm`, so invalid-percent availability was not physically observed. The direct MQTT negative test remains optional and does not block the safe AD5263 test.

### 6. AD5263 Dimmer Safe Readback Test - Complete

Existing sketch: `sketches/system-tests/07_Ad5263DimmerSafeReadbackTest`

Purpose: validate AD5263 communication, mapping, shutdown behavior, and fault reporting before applying mains relay power.

Required behavior:

- Preserve previous behavior and keep the light relay open unless an explicitly documented manual safety step allows otherwise.
- Hold or enter AD5263 shutdown during boot until configuration and intended state are reconstructed.
- Write representative brightness targets to the AD5263 using the production mapping: 0%, 50%, 100%, and selected intermediate values.
- Verify readback or plausibility according to the production light fault strategy.
- Publish `light_fault` and `light_fault_reason` only with production-relevant reason values.
- Publish every AD5263 test action immediately to dedicated Home Assistant history entities with a unique step index plus brightness and expected/readback W2/W1 values. The normal periodic status interval must not suppress intermediate run steps, and Serial must not be required for installed-system acceptance.
- Confirm that AD5263 errors keep or put the relay open.

Exit criteria:

- RDAC writes match expected mapping and readback behavior.
- `SHDN` sequencing is safe and reproducible.
- Any AD5263 failure mode leaves the lamp power relay open and records the correct fault reason.
- HA history contains every indexed online run step, including 25%, 50%, and 75%, with matching expected/readback bytes; endpoint-only history does not pass.

Confirmed result: version `1.0.1` preserved the Test 06 runtime and exposed the complete indexed Home Assistant run, exact representative readbacks, the additional 66% target, all three controlled production fault reasons with recovery between injections, an OTA interruption, and a controlled WiFi outage/recovery. The relay remained without a close path. The final soak completed without further state changes or anomalies, so Test 07 was accepted on 2026-07-29.

### 7. Light Relay And Manual HA Control Test - Light Complete, Infrastructure Reopened

Purpose: validate controlled light power sequencing and HA-mode manual light behavior after the dimmer path is proven safe.

Existing sketch: `sketches/system-tests/08_LightRelayManualHaControlTest`

Entry gate: satisfied on 2026-07-29 by the accepted Test 07 soak.

Required behavior:

- Preserve previous behavior.
- Enforce startup sequence: keep AD5263 in shutdown, load/reconstruct state, set resistance, release shutdown, then close the relay only for a valid on command.
- Enforce shutdown sequence: open relay before putting AD5263 into shutdown.
- Validate `light_auto_mode = OFF` behavior for `light.grow_light`, brightness, and on/off control.
- Validate `light_hard_power_off` as an always-available safety override that opens the relay and retains internal dimmer state.
- Keep Arduino schedule events ignored while HA mode is active.

Exit criteria:

- Manual HA light control behaves like the target production light entity only in HA mode.
- Hard power off works in all light modes and never requires hiding the entity.
- No race allows relay closure before AD5263 is configured and released safely.

Confirmed result: the version `1.0.0` run accepted the inherited runtime, safe relay/`SHDN` ordering, auto-mode rejection, hard power off, RTC neutrality, WiFi recovery, OTA safe state, and extended soak. Its ArduinoHA light omitted the `100` brightness scale, so HA callback values used the default `0..255` range and were clamped at `100`, appearing limited to approximately `39 %`. Version `1.0.1` set the production-equivalent `0..100` scale, compiled successfully, and the focused 2026-07-30 HA-history run confirmed commands and exact readbacks above `39 %`, including the full `100 %` endpoint. These light-control points remain complete and do not need repetition. Corrective version `1.0.3` carries forward the fixed SHT command spacing, verified EEPROM transactions, active-high A7 semantics, and sequenced HA infrastructure events; it remains reopened only for the focused infrastructure checks in the test README.

### 7a. Interim Local RTC Schedule Runtime Test

Purpose: validate the local schedule and remote observability needed before the broader Test 09 lamp run without
changing or replacing Test 08 or Test 09.

Sketch: `sketches/system-tests/08a_LocalLightScheduleRuntimeTest`

Status: version `1.0.2` implemented and compile-verified; reopened for corrective installed-system validation.

Required behavior:

- run both persisted DS3231 alarm targets while WiFi, MQTT, or HA is unavailable;
- use separate HA hour/minute/target entities, one shared dim duration, and persistent Hard Power Off;
- retain relay/`SHDN` continuity for every non-zero live ramp and use the full safe sequence only at zero, boot,
  OTA, activation, or fault boundaries;
- synchronize UTC through non-blocking NTP, apply Europe/Vienna DST locally, and de-duplicate transition-hour
  alarms through persisted execution dates;
- publish one-minute TSL raw channels, D9 line state, and soil raw data plus module status/error entities;
- keep the fan off and omit SHT completely; and
- use a separate HA device and EEPROM record at byte 512;
- reject invalid DS3231 BCD/calendar data and verify alarm transactions before acting;
- recover non-returning calls through a 16-second watchdog and manually released Recovery Lockout; and
- pace application HA states/events, with explicit event sources and no redundant `test_step_index`.

The v1.0.0 alarm-trigger run ended in complete HA and OTA unavailability and later exposed an impossible RTC
value. Version 1.0.1 addressed that failure with checked RTC transactions, stuck-line detection, bus recovery,
watchdog containment, manual recovery release, and paced HA publication. Version 1.0.2 additionally corrects its
retained-entity cleanup for configurable discovery prefixes and the current ArduinoHA topic layout.

Exit criteria are maintained in the test-local README. Test 08a is an explicitly requested interim validation
and does not change the entry gate or accepted results of the ordered Test 09 sequence.

### 7b. Relay-Bypass Dimmer Diagnostic Test

Purpose: isolate relay-contact mains switching from the AD5263 and firmware paths without changing the target
architecture.

Sketch: sketches/system-tests/08b_RelayBypassDimmerDiagnosticTest

Status: version 1.0.0 implemented and compile-verified; supervised installed-system validation pending.

Required behavior:

- use a separate HA device and data prefix while retaining Test 08 SHT, fan, RPM, RTC, EEPROM, soil, network,
  OTA, and combined sketch-identity behavior;
- bypass only the relay contacts; keep the relay module connected and reproduce the Test 08 coil sequence;
- preload D4 HIGH before OUTPUT mode and never assert SHDN in boot, normal, error, recovery, or OTA paths;
- use only a byte-identical verified minimum-resistance RDAC target for visible off;
- retain the last verified logical brightness on AD5263 errors, de-energize the coil, set light_fault, and never
  claim mains-off or physical lamp-off;
- publish relay-contact bypass, coil, SHDN, off-method, boot-verification latency, and complete indexed light
  snapshots through Home Assistant; and
- require supervised power sequencing and immediately available manual mains disconnection for warm reset and
  OTA validation.

A stable run shifts investigation toward relay-contact mains switching, inrush, or lamp-driver disturbance. A
repeat failure with the contacts bypassed shifts investigation toward the relay coil, I2C/dimmer path, power
integrity, or firmware. The test does not replace the mains relay and does not satisfy the System Test 09
boundary characterization.

### 8. Arduino Schedule And RTC Alarm Light Test

Purpose: validate autonomous Arduino light schedule behavior through DS3231 alarms, characterize the real
lamp across the unrestricted AD5263 command range, and verify independently configurable Alarm1/Alarm2
targets.

Sketch: `sketches/system-tests/09_ArduinoScheduleRtcLightTest`

Status: implemented as version `1.0.0`; hardware validation pending.

Required behavior:

- Derive the sketch only from the accepted Test 08 system test and preserve all previously accepted behavior.
- Validate `light_auto_mode = ON` with DS3231 Alarm1/Alarm2 as the schedule triggers on the confirmed
  `D10` / `EXTINT5` input.
- Use the RTC alarm ISR only to set a flag; evaluate alarm identity and actions in the main loop.
- Expose and persist `light_on_time_minutes`, `light_off_time_minutes`, `light_on_target_percent`,
  `light_off_target_percent`, and `light_dim_minutes`. Target range is `0..100`, step `1`, with defaults
  `100` and `0` for Alarm1 and Alarm2.
- Pass the complete `0..100 %` range through without a provisional `5 %` or other minimum-active clamp.
  Keep `0 %` as the canonical relay-off target.
- Sweep upward and downward through the complete range on the real lamp. Record the first reliably
  illuminated value, the first effective full-output value, and any hysteresis or delayed response.
- Start non-blocking dimming requests from alarm events using the matching stored target and dim duration.
  Non-zero live dim steps keep relay and `SHDN` stable; transitions from or to `0 %` retain the verified
  safe switching sequence.
- Publish every indexed alarm, dim-step, RDAC expected/readback, relay, `SHDN`, and completion state
  through Home Assistant so the run is available without Serial.
- Test non-default targets for both alarms, including a non-zero Alarm2 target, and confirm persistence plus
  clean state republish after MQTT reconnect and OTA reboot.
- Reject HA manual light commands while Arduino auto mode is active and terminate a running schedule dim
  when switching to HA control.
- Use an ArduinoHA entity registration capacity of `72`.

Exit criteria:

- Exactly one light control source is active at a time.
- Arduino schedule events control the light only when `light_auto_mode = ON`.
- Alarm1 and Alarm2 each use their own persisted HA-configured target and are rearmed for the next day.
- Commands and readbacks cover `0..100 %` unchanged so the physical dark and full-output boundaries can be
  documented from observation rather than hidden by firmware normalization.
- `0 %` always opens the relay, and no dimmer setting is treated as a substitute for hard power-off.
- HA manual light commands are rejected and the canonical state is republished while auto mode is active; HA dim-job entities remain deferred to Test 10.

### 9. HA Dimming Job And Resume-State Test

Purpose: validate HA timed dimming through the mandatory entities and restart resumption behavior.

Required behavior:

- Preserve previous behavior.
- Accept HA dimming requests only through `ha_dim_target_percent`, `ha_dim_duration_minutes`, and `start_ha_dim`.
- Start HA dim jobs only when `light_auto_mode = OFF`.
- Replace an active HA dim job with a new valid request from the active control source.
- Persist resume state using RTC/Epoch-based timing, not `millis()`.
- Reconstruct last effective brightness, hard-power-off state, and active HA dimming progress after restart or OTA update.
- Avoid unnecessary EEPROM writes while still preserving required resume information.

Exit criteria:

- Running dim jobs resume correctly after OTA/reboot.
- HA dim jobs are ignored in Arduino auto mode.
- No alternative JSON/string command path is introduced.

### 10. Network Outage, Republish, And Fallback Test

Purpose: validate production-like recovery after WiFi/MQTT/Home Assistant outages and the ten-minute light fallback rule.

Required behavior:

- Preserve previous behavior.
- Exercise WiFi loss, MQTT broker outage while WiFi remains connected, Home Assistant restart, and board OTA reboot.
- Keep local sensor, fan, RTC, EEPROM, and light schedule logic running during outages.
- After startup or MQTT reconnect, actively republish switch states, light state, number configuration values, fault states including `light_fault_reason`, and current sensor values.
- Validate `light_fallback_to_auto`: after connection loss exceeds ten minutes, either switch to Arduino auto mode or turn the light off according to the configured setting.
- Keep OTA available during all reconnect and fallback behavior.

Exit criteria:

- State republish is complete and does not require HA-side guesswork.
- Fallback behavior begins only after the documented timeout and remains safe.
- MQTT reconnect retry is periodic while WiFi is connected and does not depend only on a WiFi state transition.

### 11. Full Integrated Soak And Fault Injection Test

Purpose: validate the complete installed system over an extended period with representative commands, outages, and recoveries.

Required behavior:

- Preserve all confirmed behavior from earlier sketches.
- Run all production-relevant modules together with production-relevant HA entities.
- Capture direct MQTT test events to ignored JSONL files for later analysis.
- Inject or simulate representative faults where safe: missing I2C device, AD5263 write/readback failure, fan tach absence, SHT reset status, MQTT outage, WiFi outage, invalid soil depth, and RTC/NTP failure classes where practical.
- Keep actuator safety rules active during all injected faults.

Exit criteria:

- The complete entity model behaves consistently after boot, reconnect, OTA update, faults, and recovery.
- Mandatory fault entities are understandable in Home Assistant.
- README findings list remaining production-firmware gaps or explicitly state that requirements are validated by the system-test sequence.

## Completion Criteria For The System-Test Series

The series is complete when the final integrated test README summarizes confirmed coverage for these requirement groups:

- OTA, WiFi reconnect, MQTT reconnect, and republish behavior
- safe boot, safe outage, safe OTA, and safe reset states for all connected actuators
- I2C device coexistence without ISR bus access
- SHT alert thresholds and fan control responsibility separation
- fan tach/RPM and `fan_fault`
- soil raw/percent measurement and HA-controlled calibration
- AD5263 mapping, readback/fault handling, and `SHDN` sequencing
- light relay sequencing and hard-power-off override
- strict separation of HA light control and Arduino auto schedule control
- DS3231 alarm scheduling through Alarm1 and Alarm2
- HA dimming through only the defined number/button entities
- external AT24C32 persistence and RTC/Epoch-based resume state
- fault entities and `light_fault_reason`
- privacy-safe capture and documentation of results
