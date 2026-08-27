# I2C Passive Baseline Test

This maintenance revision also uses the shared retained-entity manifest for the `Grow Controller Tests` device. On connection it removes discovery for every known Test 02-09 entity not active in this sketch and clears orphaned states from the other known shared data prefix. Cleanup uses the existing ArduinoHA connection and publishes at most one retained deletion per loop pass.

Revision `02_I2cPassiveBaseline v1.2.4` retains the per-probe evidence from the diagnostic revisions while restoring the proven ArduinoOTA internal-storage path. It still probes only the five documented addresses and never initializes a peripheral, accesses a register, clears an interrupt source, clocks a stuck bus, or attempts electrical bus recovery.

The WiFi-to-ArduinoHA lifecycle correction from `v1.1.2` remains in place. Version `v1.2.4` omits the SAMD hardware watchdog completely, delays I2C scans until the first MQTT boot publication, omits the unnecessary initial `WiFi.disconnect()`, and passes the standard `InternalStorage` object directly to ArduinoOTA.

## Purpose

- Distinguish a device-specific NACK from a bus-wide failure while every actuator remains safe.
- Publish each completed scan through Home Assistant using the established **Grow Controller Tests** device and one ArduinoHA/MQTT connection.
- Preserve every isolated failure in counters, the last-error description, and a sequenced test step without keeping a recovered device in a current fault state.
- Retain enough HA history to identify the last started I2C probe if a call stops returning.

## Hardware And Safe State

The five probes are fixed to:

| Device | Address |
|---|---:|
| SHT31 replacement candidate | `0x44` |
| DS3231 | `0x68` |
| AT24C32 | `0x57` |
| AD5263 | `0x2C` |
| TSL25911 | `0x29` |

Fan pin `2` is held `LOW`, light-relay pin `3` is held `LOW`, and AD5263 `SHDN` pin `4` is held `LOW`. No later code path changes these outputs.

The SHT ALERT line on `A7` is configured as `INPUT` and polled as active-high. The RTC alarm line on pin `10` is configured as `INPUT_PULLUP` and polled as active-low. No SHT, RTC, or tach interrupt is attached in this passive test. The sketch does not clear either external interrupt source.

After `Wire.begin()`, the sketch enables the SAMD PORT input buffers for SDA and SCL without changing their peripheral multiplexing. The three-sample preflight therefore reports the actual electrical line levels. If either line is low, that scan is marked `bus_stuck` and no Wire transaction is started. All device availability states then become unavailable for the current scan, while device error counters remain unchanged because no device transaction was attempted. The bus status reports RTC and AD5263 as `not_probed` instead of repeating stale results from the last complete scan.

## Home Assistant Evidence

The sketch uses device ID `grow_controller_tests_persistence_rtc` and prefix `smaeenhouse/test/i2c_passive_baseline/ha`. `Sketch Identity` publishes `02_I2cPassiveBaseline v1.2.4` once per MCU boot. The obsolete retained JSON status topic from older revisions is cleared through the same ArduinoHA MQTT connection.

Every 30 seconds a scan publishes:

- scan sequence and a sequenced `I2C Test Step` commit marker;
- per-device availability, total probe failures, and consecutive probe failures;
- SDA and SCL levels;
- a separate `I2C Probe Phase` update immediately before and after every address probe;
- boot reset cause and a cumulative count of scans skipped because SDA or SCL was already low;
- the last failing device, address, Wire result code, and decoded meaning;
- SHT ALERT and RTC alarm line levels;
- safe-output, uptime, network-recovery, and OTA-gap states.

The scan step also records whether DS3231 and AD5263 responded in that same scan. This makes a failure attributable to one device or to a wider bus disturbance without adding active recovery behavior.

After the first successful MQTT boot-state publication, passive scans are enabled. No hardware watchdog is armed. ArduinoOTA uses the library's standard `InternalStorage` path without a storage wrapper, so its receive, staging, flash-copy, and system-reset sequence matches the previously proven OTA tests. Reset-cause publication remains read-only diagnostics and does not configure the watchdog.

## Compile And Upload

```powershell
$env:SKETCH = "sketches/system-tests/02_I2cPassiveBaseline"
.\scripts\check-arduino.ps1
```

Upload by OTA from the preceding test, or by USB when OTA is unavailable. Credentials remain in the ignored local `Credentials.h`.

## Acceptance Procedure For v1.2.4

1. Remove power from the controller and every I2C module, connect the replacement SHT31 while unpowered, restore power, and confirm identity `v1.2.4`, a plausible reset cause, and safe actuator outputs. Do not hot-plug the sensor into a running or stuck bus.
2. Require at least ten complete scans with SDA/SCL high, `started` and `returned` phases for all five devices, SHT31 responding at `0x44`, and no new device or bus-stuck errors before starting the soak.
3. Keep the lamp physically disconnected from mains and run for at least four hours, exceeding the previous approximately 51-minute failure point. HA, OTA, complete scans, and all five device probes must remain available without new NACKs or skipped scans.
4. Disable WiFi for a controlled interval. Passive scans and safe outputs must continue locally; after reconnect, the advanced scan sequence and all current states must be republished.
5. If a scan stops returning, retain the last `started` phase. If SDA or SCL is low, require `bus_stuck_skipped`, all devices unavailable or `not_probed`, an incremented stuck counter, continued HA/OTA service, and no Wire transaction.
6. Confirm throughout the run that fan, relay, and `SHDN` stay in their safe electrical states. Do not provoke or add electrical bus recovery.
7. Perform a final OTA upload and require a successful flash, reset cause `system`, identity `v1.2.4`, the delayed first scan, and resumed complete probe phases.

If a probe returns an error, retain its Wire code and the same scan's RTC/AD5263 availability. If a scan blocks, retain the last probe phase. These observations decide whether the next investigation is device-specific, bus-wide, or outside I2C.

## Limitations

An address acknowledgement proves transport presence only. It does not validate sensor values, RTC time, EEPROM contents, AD5263 readback, or TSL25911 measurement quality.

## Results And Notes For The Next Test

- Current status: **Reopened** for diagnostic revision `v1.2.4`; replacement-SHT bring-up, the four-hour isolated soak, WiFi recovery, and final OTA are pending.
- Revision `v1.1.1` observation: with the SHT31 disconnected, HA became unavailable after one to two hours while OTA remained reachable. Code review traced this to a mismatched ArduinoHA initialization guard after a transient WiFi disconnect, not to the expected SHT address NACKs.
- Revision `v1.1.2` proved missing-SHT handling, SHT hot-plug recovery, and WiFi/MQTT recovery, but a later connected-SHT run stopped both HA and OTA between scan 82 and the expected scan 83 at approximately 2,430 seconds uptime. The last completed scan showed all five devices available with zero errors, so the new revision must capture the in-progress operation rather than infer the cause from the previous completed scan.
- Revision `v1.2.0` armed the watchdog in `setup()`. Because the installed NINA SPI transport contains an unbounded `waitForSlaveReady()` loop that does not feed the watchdog, the first post-OTA WiFi transaction could create repeated watchdog resets before MQTT or OTA initialized. Revision `v1.2.1` corrects that boot-order regression.
- Revision `v1.2.1` restored boot and produced stable passive scans with no peripherals connected, but three OTA attempts ended in reset cause `watchdog` after upload completion. The watchdog remained active while `InternalStorage.apply()` disabled interrupts and copied flash, so its pre-apply feed could not cover the complete operation.
- Revision `v1.2.2` attempted to disable the watchdog immediately before flash apply. Two observed OTA attempts still reset by watchdog after approximately one timeout period, and Arduino reported `Error flashing the sketch`. Because the board's effective watchdog state could not be proven safe across the non-returning flash-copy path, `v1.2.3` removes runtime watchdog activation and the OTA storage wrapper instead of adding another flash-path experiment.
- Revision `v1.2.3` stayed available for more than four hours without the SHT31 or lamp, proving that expected missing-SHT NACKs no longer take HA or OTA offline. After 102 complete scans, however, SDA became persistently low and the sketch correctly skipped the next 424 scans while remaining online. Two TSL25911 NACKs and one recovered DS3231 and AT24C32 NACK occurred earlier in that boot. A small simultaneous smart-switch voltage/frequency/current change is retained as correlation only; it does not prove a logic-supply fault.
- Revision `v1.2.4` probes the replacement SHT31 at its fixed default address `0x44` and removes stale per-device availability claims from bus-stuck scans. It does not add active bus recovery.
- Historical result: the original baseline was accepted on 2026-07-15 after correcting the SHT address from `0x44` to the installed `0x45`. DS3231, AT24C32, AD5263, and TSL25911 were present and the actuator outputs stayed safe.
- Historical limitation: the original long run later stopped publishing after 63,916 seconds. Later tests added NINA recovery counters and demonstrated much longer connectivity, but this new revision deliberately rechecks network recovery without combining it with active I2C recovery.
- Carry-forward rule: keep the lamp disconnected for this focused I2C run. If SDA becomes persistently low again, correlate the onset with power measurements and repeat with an independently powered controller/I2C domain before adding electrical bus recovery. Propagate `0x44` to other tests and production only after this passive run succeeds, then validate active SHT transactions in Test 03.
- Privacy: keep Home Assistant exports and real measurements in ignored local storage; do not commit them.
