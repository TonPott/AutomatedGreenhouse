# SHT Hardware Baseline Test

This maintenance revision also uses the shared retained-entity manifest for the `Grow Controller Tests` device. On connection it removes discovery for every known Test 02-09 entity not active in this sketch and clears orphaned states from the other known shared data prefix. Cleanup uses the existing ArduinoHA connection and publishes at most one retained deletion per loop pass.

Revision `03_ShtHardwareBaseline v1.2.1` reopens the installed SHT31 baseline with a non-blocking transaction state machine and datasheet-compliant separation between periodic measurement and register commands. It does not change `SHTa.h`, `SHTa.cpp`, or production firmware.

## Purpose

- Validate the SHT31 at the fixed project address `0x45`.
- Prevent status and alert-limit commands from being sent directly while periodic acquisition is active.
- Preserve isolated transaction errors as sequenced Home Assistant evidence while making the binary fault represent a current, confirmed failure.
- Verify the four existing alert-limit registers through an unchanged, byte-identical round trip.
- Validate the installed active-high SHT ALERT signal on `A7`.

## Safe State And Interrupt

Fan pin `2` remains off, light-relay pin `3` remains open, and AD5263 `SHDN` pin `4` remains asserted. `A7` is configured as `INPUT`, validated through `digitalPinToInterrupt(A7)`, and attached with `RISING` for the SHT31 push-pull active-high signal. The ISR only sets a flag. All SHT, Wire, Home Assistant, and MQTT work runs in normal loop context.

## Transaction Model

Initialization advances without blocking delay chains:

1. probe `0x45`;
2. send Break/stop-periodic;
3. wait at least 1 ms;
4. soft-reset and wait for reset completion;
5. read status and all four limits while idle;
6. clear status;
7. start periodic measurement.

The first measurement is requested by the regular two-second schedule. While periodic mode is active, the sketch sends only `Fetch Data`.

An A7 event, a manual round trip, or recovery enters a controlled stopped transaction. After a successful stop, the state machine waits at least 1 ms, performs only the required status/limit operations, clears status when required, and makes exactly one periodic restart attempt. There are no periodic 30-second background limit reads and no status command after every sample. This follows the SHT3x requirement to stop periodic acquisition before commands other than `Fetch Data`: [Sensirion SHT3x-DIS datasheet](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf).

The round-trip button captures all four raw limits, writes the unchanged values, reads all four back, and requires byte equality. It aborts at the first failed operation and does not issue an immediate rollback series on a disturbed bus.

## Fault And Recovery Semantics

- One failed `Fetch Data` increments measurement and historical error counters and publishes a sequenced step, but does not by itself assert `SHT Fault`.
- Three consecutive measurement failures assert the fault and schedule recovery after 30 seconds.
- Failed initialization, stop, status, limit capture/write/readback, or periodic restart asserts the fault immediately.
- A successful controlled measurement pause does not assert the fault.
- Recovery is complete only after reinitialization plus one valid measurement. Only then does the current fault clear; historical counters remain monotonic.
- `SHT Last Error` contains the operation and both the Sensirion code and decoded class. In particular, `268` is described as a write/address NACK and `527` as a read with insufficient data.

## Home Assistant Evidence

One ArduinoHA connection publishes the combined identity `03_ShtHardwareBaseline v1.2.1`, temperature, humidity, current fault/health states, active-high alert-line state, interrupt attachment, raw status/limit values, per-operation error counters, consecutive/max-consecutive failures, recovery and round-trip counters, last error, round-trip result, and sequenced test steps. Boot steps are buffered until Home Assistant connects. The separate retired sketch-name/version entities and older direct status topic remain removed.

## Compile And Upload

```powershell
$env:SKETCH = "sketches/system-tests/03_ShtHardwareBaseline"
.\scripts\check-arduino.ps1
```

## Acceptance Procedure For v1.2.1

1. Install by OTA and confirm the combined identity, `A7` interrupt attachment, and the complete initialization sequence.
2. Require at least five regular valid measurements without startup NACK, short read, or recovery-counter increase.
3. Run three unchanged-limit round trips at least 30 seconds apart. Require exact readback, one restart per run, continuing measurements, and `SHT Fault=off` throughout each successful transaction.
4. During a 30-minute soak, require no periodic status/limit transactions and no unexplained NACK or short read.
5. Treat an isolated error as retained evidence; verify from counters and steps that it does not become a permanent fault. Any failed stop/limit/readback/restart path must assert the current fault.
6. Perform a final OTA upload and require successful reinitialization and resumed measurements.

## Results And Notes For The Next Test

- Current status: **Reopened** for revision `v1.2.1`; hardware acceptance is pending.
- Historical result: the original read-only baseline proved stable communication at `0x45`, plausible temperature/humidity, readable limits, safe outputs, and a run beyond 183,000 seconds.
- Historical evidence retained: a later run recovered from isolated measurement error `268`; revision `v1.1.0` also showed startup status error `527` and brief false fault pulses during otherwise successful limit round trips. These observations motivate, but do not count as acceptance of, the new state machine.
- Carry-forward rule: Test 05 may adopt this routine only after this revision completes three round trips, the 30-minute soak, and final OTA without unexplained errors.
- Privacy: no Home Assistant export or raw environmental history belongs in the repository.
