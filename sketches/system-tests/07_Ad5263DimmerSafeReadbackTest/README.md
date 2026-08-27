# AD5263 Dimmer Safe Readback Test

Sketch: `07_Ad5263DimmerSafeReadbackTest.ino`

Version: `1.0.3`

## Status

Complete. Version `1.0.0` exposed the installed AD5263 run but did not provide sufficient remote history resolution. Version `1.0.1` passed the functional HA-history run, intermediate `set:66`, all controlled fault injections with recovery, OTA, controlled WiFi outage/recovery, and the final soak without further state changes or anomalies.

This maintenance revision also uses the shared retained-entity manifest for the `Grow Controller Tests` device. On connection it removes discovery for every known Test 02-09 entity not active in this sketch and clears orphaned states from the other known shared data prefix. Cleanup uses the existing ArduinoHA connection and publishes at most one retained deletion per loop pass.

## Purpose

This OTA-capable system test adds the production AD5263 brightness mapping, write/readback verification, shutdown sequencing, and light-fault reporting to the confirmed Test 06 runtime. It exercises only the low-voltage dimmer path. The lamp power relay has no close path in this sketch and must remain open for the complete test.

## System-Test Lineage

The sketch inherits the confirmed network, OTA, EEPROM/RTC, SHT alert, fan-control, and soil-calibration runtime from `06_SoilMoistureCalibrationTest`. The dimmer controller is a test-local snapshot of the current production `LightController` mapping and fault strategy. No hardware-test sketch is used as a source.

## Hardware Under Test

- Arduino Nano 33 IoT
- AD5263 at I2C address `0x2C`, with `DIS=1`
- AD5263 `SHDN` on `D4`, with the external 10 kOhm pull-down documented by the project
- Light power relay on `D3`, held open for the entire test
- All sensors, fan, DS3231, and AT24C32 paths retained from Test 06

The project schematic is the primary wiring reference. The dimmer resistance path is:

```text
Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-
```

Do not modify the installed wiring for this test.

## Safe Boot And Runtime Behavior

Before Serial, I2C, storage, sensors, or networking are initialized, the sketch:

- opens the light relay
- asserts AD5263 `SHDN`
- commands the fan off

After I2C starts, the sketch probes `0x2C`, writes the safe 0% RDAC target, and verifies both readback bytes while `SHDN` remains asserted. A write, readback, or presence fault immediately opens the relay again and asserts `SHDN`.

The test controller deliberately exposes no relay-on method. It enforces the open relay in every loop and before every dimmer operation. The only normal `SHDN` release lasts two seconds, occurs with the relay open, and is followed by `SHDN` assertion plus a verified 0% target.

All I2C, MQTT, and Home Assistant work happens in normal loop context. Existing ISRs remain limited to flags or pulse counting.

## Production Mapping Under Test

The controller writes W2 first and W1 second, then selects and reads both RDAC channels. It makes at most two complete write/readback attempts.

| Brightness | Expected W2 | Expected W1 |
| ---: | ---: | ---: |
| 0% | 0 | 255 |
| 25% | 0 | 128 |
| 50% | 0 | 0 |
| 75% | 127 | 0 |
| 100% | 255 | 0 |

Only exact byte readback is accepted. A failed operation retains the safe relay and shutdown states.

## Home Assistant Entities

The ArduinoHA capacity remains `64`. Test 07 preserves all 48 Test 06 entities and adds exactly nine entities, for 57 total:

- `binary_sensor.light_fault`
- `sensor.light_fault_reason`
- `sensor.ad5263_test_step`
- `sensor.ad5263_step_index`
- `sensor.ad5263_brightness_percent`
- `sensor.ad5263_expected_w2`
- `sensor.ad5263_expected_w1`
- `sensor.ad5263_readback_w2`
- `sensor.ad5263_readback_w1`

The production-relevant fault reasons are:

- `ad5263_not_found`
- `ad5263_write_failed`
- `ad5263_readback_mismatch`

An empty reason means no light fault. `sensor.sketch_identity` remains the single sketch identity entity and publishes `07_Ad5263DimmerSafeReadbackTest v1.0.3` once per MCU boot.

The inherited `binary_sensor.persistence_rtc_shdn_safe` reports the electrical safe level. It may intentionally become `off` during the documented two-second, relay-open `SHDN` release; that observation is not itself a fault.

### Home Assistant History Collection

Serial output is optional diagnostics only and is not required for installed-system acceptance. MQTT Explorer may still send a non-retained command, but it is not the test-history source.

Every AD5263 action publishes a dedicated HA snapshot immediately, independently of the normal 30-second HA full-state interval and the 10-second direct diagnostic interval. Numeric values are force-published for each snapshot. `sensor.ad5263_test_step` is published last as a commit marker and contains `<step index>:<step name>`, so consecutive runs remain distinguishable in HA history.

An online `run` must show these committed steps in order:

1. `ad5263_sequence_started`
2. `ad5263_set_0_ok`
3. `ad5263_set_25_ok`
4. `ad5263_set_50_ok`
5. `ad5263_set_75_ok`
6. `ad5263_set_100_ok`
7. `ad5263_shdn_released_relay_open`
8. `ad5263_safe_zero_restored`
9. `ad5263_sequence_complete`

For each step, compare the same-history-time values of brightness, expected W2/W1, readback W2/W1, `light_fault`, relay safe, and `SHDN` safe. The start marker is held for one full second before the 0% operation, so it cannot be overwritten in the same loop. A failed snapshot remains pending across normal loop retries until the next test action. Any missing step index invalidates the HA history and requires a new online run.

HA cannot receive live samples while WiFi or MQTT is intentionally unavailable. Therefore, first validate a complete online run from HA history. Treat the later outage run as a separate local-continuity test and verify its final state and counters after reconnect.

## Direct MQTT Interface

Detailed diagnostics are published in normal loop context under:

```text
smaeenhouse/test/ad5263_safe_readback/status
smaeenhouse/test/ad5263_safe_readback/event
```

Commands are received under:

```text
smaeenhouse/test/ad5263_safe_readback/cmd
```

Publish commands without retain. The sketch clears any retained command before subscribing so an old `run` cannot be repeated after reboot.

Supported payloads:

- `run`: perform the complete non-blocking 0/25/50/75/100%, release, shutdown, and safe-zero sequence
- `readback`: verify the current target again
- `set:<0..100>`: apply and verify one brightness target while shutdown remains asserted
- `shdn_release`: release shutdown for two seconds with the relay open, then assert it again
- `shdn_assert`: assert shutdown immediately
- `inject_not_found`: probe the documented unused address `0x2D` and report `ad5263_not_found`
- `inject_write_failed`: inject the production write-failure result without an unsafe bus operation
- `inject_readback_mismatch`: read the real device and then inject the production mismatch result
- `recover`: probe `0x2C`, reapply the current target, verify it, and clear a recoverable fault

The three injection commands validate the safe-state and reporting paths. They are controlled test injections, not substitutes for a later physical open-circuit test if one is desired.

The periodic status JSON includes AD5263 presence, brightness, expected and read W2/W1 bytes, relay and shutdown states, fault and reason, sequence state, command/run counters, apply/verify/retry counts, and injected-fault count, HA step index, and last committed step. It also includes the inherited network, SHT, fan, soil, RTC, EEPROM, and OTA diagnostics.

## Credentials And Compile Check

Copy `Credentials.example.h` to the ignored local `Credentials.h` for upload. Never commit real credentials.

Compile from the repository root:

```powershell
$env:SKETCH = "sketches/system-tests/07_Ad5263DimmerSafeReadbackTest"
.\scripts\check-arduino.ps1
```

On Linux / Codex Cloud / GitHub Actions:

```bash
SKETCH=sketches/system-tests/07_Ad5263DimmerSafeReadbackTest ./scripts/check-arduino.sh
```

The compile script may create `Credentials.h` from the example only when none exists and must remove that temporary file afterward.

## Installed-System Acceptance Procedure

1. Compile the sketch. Confirm temporary credential cleanup and the static rule that ISRs only set flags or count pulses.
2. Install version `1.0.1` by OTA. Confirm that the light relay never closes, `SHDN` is asserted during boot, the fan initially remains off, and all behavior accepted through Test 06 remains available.
3. In Home Assistant, confirm the combined sketch identity and the startup step `ad5263_startup_safe_target_ok`, with brightness `0`, expected/readback W2/W1 `0/255`, and no light fault. Serial access is not required.
4. Publish non-retained `run`. In HA history, confirm all nine documented indexed steps and exact readback for 0%, 25%, 50%, 75%, and 100%. Seeing only endpoints is a failure. The relay must remain open at every stage.
5. Confirm the controlled `SHDN` release lasts about two seconds only while the relay is open. Confirm the sequence then asserts `SHDN`, restores/verifies `0/255`, and reports completion.
6. Exercise `set:25`, `set:50`, `set:75`, and `readback` individually. Confirm the expected mapping and no fault or unexplained retry.
7. Run each injection separately. Confirm the matching Home Assistant fault and reason, open relay, and asserted shutdown. Send `recover` after each one and confirm the actual device is reprobed, the target is verified, and the fault clears.
8. After the complete online HA-history run has passed, start a separate `run`, disconnect WiFi after the command is accepted, and restore it after at least three reconnect timeouts and one NINA reset. Confirm the local sequence completes safely from its final state/run counter after reconnect, MCU uptime does not restart, recovery counters increase, and all non-identity HA states are republished. Offline intermediate steps cannot be delivered live to HA.
9. Perform an OTA update. Confirm the relay remains open through update/reboot, the safe target is reconstructed before any controlled shutdown release, the sketch identity changes only when its version changes, and the EEPROM boot/sequence behavior remains correct.
10. Leave the controller at 0% with `SHDN` asserted for at least two hours. Reject unexpected relay closure, persistent light fault, readback drift, continuous EEPROM writes, OTA poll gaps, or regressions in inherited RTC, SHT, fan, soil, and network behavior.

Stop immediately if the light relay closes at any point. All required Test 07 observations are now confirmed, so Test 08 may proceed under its own safety and acceptance procedure.

## Safety And Privacy

- The lamp power relay must stay open for the entire test.
- Do not bypass `SHDN`, bridge relay contacts, or apply a manual relay command.
- Fault injection is software-controlled and must not involve shorting I2C or dimmer wiring.
- Do not commit MQTT captures, HA exports, sensor histories, calibration data, or other real-environment records.

## Known Limitations

- The test verifies digital RDAC commands and readback, not effective resistance at the lamp driver terminals.
- Injected not-found, write-failed, and mismatch results test the production fault paths without physically disturbing the installed bus.
- The test does not close the light relay or assess lamp brightness. Those actions belong to the next system test only after this test passes.
- The accepted functional result used the historical D7 SHT polling fallback. Version `1.0.2` changes only the active SHT ALERT test configuration to `A7`; the shared A7 interrupt acceptance remains pending.

## Results And Notes For The Next Test

- Confirmation status: Complete.
- Date / firmware revision: Version `1.0.1`, accepted on 2026-07-29.
- Required observations: Complete; the final soak produced no further changes, unexpected relay closure, persistent light fault, readback drift, continuous EEPROM writes, OTA poll gaps, or inherited-runtime regression.
- Anomalies or limitations: Version `1.0.0` showed only endpoints because its publication interval was too coarse. Version `1.0.1` exposed the complete indexed run, the additional 66% setpoint, each controlled injection with recovery, the OTA interruption, and the longer WiFi outage. Digital readback does not independently measure effective resistance or lamp-driver response.
- Safety notes to carry forward: Test 08 may introduce relay closure only after Test 07 is accepted. It must configure and verify the RDAC target, release `SHDN`, and only then close the relay; shutdown must open the relay first.
- Entity or topic notes to carry forward: Preserve the current HA device identifier, data prefix, single combined sketch identity, all 48 inherited entities, the two light-fault entities, and the seven AD5263 history entities. Keep low-level dimmer diagnostics on the direct `ad5263_safe_readback` status/event topics.
