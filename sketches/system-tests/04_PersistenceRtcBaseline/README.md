# Persistence RTC Baseline Test

This maintenance revision also uses the shared retained-entity manifest for the `Grow Controller Tests` device. On connection it removes discovery for every known Test 02-09 entity not active in this sketch and clears orphaned states from the other known shared data prefix. Cleanup uses the existing ArduinoHA connection and publishes at most one retained deletion per loop pass.

Revision `04_PersistenceRtcBaseline v1.2.1` reopens EEPROM validation while retaining the already accepted DS3231 logic as an independent control channel. It separates transport availability, the latest transfer, record validity, recovery state, and the binary current fault.

## Purpose

- Reduce AT24C32 traffic while preserving full pre-read and byte-identical verification.
- Distinguish a transient address/read failure from a corrupt readable record or failed write/readback.
- Never replace unknown or damaged data with defaults merely because a read failed.
- Preserve the last verified RAM record and reset Home Assistant commands to that state when persistence fails.
- Keep RTC alarm behavior, OTA, network recovery, and safe outputs unchanged.

## Safe State And Interrupts

Fan pin `2` remains off, light-relay pin `3` remains open, and AD5263 `SHDN` pin `4` remains asserted. `Fan Auto Mode` changes only the test record. The unused SHT interrupt is not configured. The RTC ISR only sets a flag and the tach ISR only counts pulses; all RTC, EEPROM, Home Assistant, and MQTT work runs in the main loop.

## EEPROM Record And Write Algorithm

The existing test record format and address remain unchanged. A transaction performs:

1. address probe and complete pre-read;
2. record-structure and checksum validation;
3. comparison with the requested complete record;
4. writes only contiguous changed ranges, split at the AT24C32 32-byte page boundaries;
5. writes body/padding changes first and checksum bytes last;
6. reads the complete record back and requires byte identity, valid structure, and valid checksum.

An unchanged record takes the verified skip path. This avoids the extra read performed by byte-wise `update()` for every byte while also avoiding writes to unchanged cells. The implementation uses the JC_EEPROM block/page API: [JC_EEPROM documentation](https://github.com/JChristensen/JC_EEPROM).

Only a complete readable record containing `0xFF` in every byte is treated as uninitialized and may receive defaults. A transport/read failure or a readable non-empty record with invalid structure/checksum never triggers an automatic default write. A corrupt non-empty record remains faulted until the operator invokes the existing verify path, which may deliberately rewrite the last verified RAM record.

## Fault And Recovery Semantics

- The first independent probe or read transport failure records `DEGRADED`, schedules a read-only retry after 10 seconds, and preserves the failure in counters and test steps.
- A second independent transport failure confirms `EEPROM Fault`.
- A write failure, full-readback mismatch, or readable non-empty invalid record asserts the fault immediately.
- Successful probe, complete read, record-shape check, and checksum check clear a transport-only fault. Historical error and recovery counters remain.
- Corrupt data is never cleared merely because the device responds again.
- A failed `fan_auto_mode` transaction leaves the last verified RAM state active and republishes that switch state.

The Home Assistant diagnostics separately expose transport status, last-transfer success, record status/validity, consecutive failures, recovery pending, recovery attempts/successes, write-range and byte counts, and the last error with transaction phase and raw code.

## RTC Control Channel

The already accepted RTC logic is unchanged. It configures Alarm1 near `now + 1 minute` and Alarm2 near `now + 2 minutes`. The ISR sets a flag; the loop checks and clears the alarm flags and publishes counters. No alarm actuates the light.

## Compile And Upload

```powershell
$env:SKETCH = "sketches/system-tests/04_PersistenceRtcBaseline"
.\scripts\check-arduino.ps1
```

The sketch uses one ArduinoHA/MQTT connection, republishes functional and diagnostic states after reconnect, and publishes `04_PersistenceRtcBaseline v1.2.1` as the combined identity once per boot.

## Acceptance Procedure For v1.2.1

1. Install by OTA and confirm successful probe, boot read, structure validation, checksum validation, and the expected RTC alarm configuration.
2. Press `Verify Persistence Record` without changing data. Require the verified skip path and no additional written range or byte.
3. Toggle `Fan Auto Mode` at least five times. Each accepted change must show pre-read, changed-range write, full readback, byte-identical verification, increasing sequence, and unchanged physical fan output.
4. Confirm Home Assistant records the number and total size of changed ranges for each transaction.
5. Run for 30 minutes without commands and require no additional EEPROM writes.
6. Perform a final OTA upload and verify persisted state, boot/sequence progression, current fault off, and successful boot transactions.

Any failed command must leave the prior verified state active. A readable corrupt record must remain visible and must not be overwritten automatically.

## Results And Notes For The Next Test

- Current status: **Reopened** for revision `v1.2.1`; focused hardware acceptance is pending.
- Historical result retained: the baseline passed on 2026-07-20 after an approximately 18-hour run with both RTC alarms, WiFi recovery, OTA, stable safe outputs, and persistent boot/sequence/checksum values.
- Historical focused result retained: revision `v1.1.0` passed on 2026-08-10. `Fan Auto Mode OFF -> ON -> OFF`, unchanged verification, and OTA restoration succeeded without fan actuation; EEPROM phase entities remained healthy.
- Reason for reopening: later combined tests repeatedly reported EEPROM faults while RTC and AD5263 remained healthy. The new revision must determine whether those events are isolated transport failures, actual record damage, or write/readback failures before the routine is reused by Test 05.
- Carry-forward rule: do not transfer this EEPROM routine into later tests until the five writes, 30-minute no-write soak, and final OTA pass without unexplained errors.
- Privacy: raw Home Assistant exports remain local and ignored.
