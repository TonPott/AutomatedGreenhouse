# Relay-Bypass Dimmer Diagnostic Test

## Purpose

08b_RelayBypassDimmerDiagnosticTest is a diagnostic fork of System Test 08 version 1.0.3.
It separates lamp mains-contact effects from the AD5263 dimming path:

- the lamp is connected directly to mains and remains powered;
- only the relay contacts are bypassed;
- the relay module and coil remain connected and reproduce the Test 08 switching sequence;
- the AD5263 is the only mechanism that requests visible light off.

This test does not change the production architecture. Production still requires the mains relay for a canonical
and electrically meaningful OFF state.

Sketch identity: 08b_RelayBypassDimmerDiagnosticTest v1.0.0.

## Electrical safety and connection order

This is a supervised mains diagnostic. Visible darkness is not galvanic or energetic isolation. The lamp driver
and power supply remain energized at every brightness, including 0 %.

1. Wire and power the Arduino, AD5263, and all low-voltage modules first.
2. Bypass only the relay contacts. Keep the relay module connected to the Arduino.
3. Connect lamp mains last.
4. Keep immediate manual mains disconnection within reach for warm-reset and OTA tests.
5. Never remove controller or AD5263 power while leaving the lamp powered.

The installed D4 hardware has an external 10 kOhm pull-down. During MCU reset that pull-down asserts AD5263
SHDN. In the installed series rheostat path, shutdown opens terminal A while retaining the W-to-B connection.
The lamp therefore sees an approximately open dimming path and may briefly go to full brightness. A supervised
warm reset or OTA is expected to expose this flash until firmware releases SHDN and verifies the 0 % target.

## Installed lamp observations

The diagnostic uses these observed installation properties without converting them into firmware clamps:

- very small resistance or a real short circuit: lamp dark and stable;
- observed dark interval: approximately 1..10 kOhm;
- effective full brightness: commonly reached at approximately 80 kOhm;
- open or approximately infinite dimming resistance: full brightness.

The dark and full-output boundaries are not final. System Test 09 must measure them systematically in both sweep
directions. The AD5263 SHDN state is also not electrically identical to disconnected dimming wires because
internal switch paths, leakage, and protection structures remain present.

The local Vitrine PDF does not contain a machine-readable statement about this dimming behavior and is not
used as evidence for the diagnostic semantics. The component behavior is based on the AD5263 datasheet and the
installed-lamp observations above.

## Diagnostic light behavior

D4 is preloaded HIGH before it becomes an output and remains HIGH in boot, normal control, errors, recovery, and
OTA handling. This sketch contains no D4 LOW write.

For each requested brightness:

1. de-energize the relay coil;
2. keep SHDN released;
3. write W2 followed by W1 using the established monotonic Test 08 mapping;
4. read both RDAC values back and require byte-identical values;
5. for a target above 0 %, energize the relay coil;
6. for 0 %, leave the relay coil de-energized.

The contacts are bypassed, so coil state cannot remove lamp power. light.grow_light = OFF means only
“verified minimum-resistance target”; it never means mains off.

On an AD5263, I2C, write, or readback error, the sketch:

- de-energizes the relay coil;
- keeps SHDN released;
- retains the last verified logical brightness and RDAC target;
- sets light_fault and publishes the precise reason;
- reports the physical light state as uncertain instead of claiming the lamp is off.

Recovery uses the existing controlled probe/write/readback path. It never asserts SHDN.

At OTA start, the sketch de-energizes the coil and attempts to write and verify 0 % while keeping SHDN
released. Failure sets the normal light fault state, but cannot guarantee lamp shutdown without working relay
contacts.

## Home Assistant

This sketch creates a separate device:

- device ID: grow_controller_test_relay_bypass_dimmer
- data prefix: smaeenhouse/test/relay_bypass_dimmer/ha
- entity limit: 64

It retains the Test 08 SHT, fan, RPM, RTC, EEPROM, soil, network, OTA, threshold, and combined sketch-identity
entities. It does not expose Light Hard Power Off.

Diagnostic entities added or redefined for this test:

- light.grow_light: requested and last verified logical brightness;
- binary_sensor.relay_contacts_bypassed: always ON;
- binary_sensor.relay_coil_energized: actual D3 output state;
- binary_sensor.shdn_released: actual D4 output state;
- sensor.light_off_method: minimum_resistance_only;
- sensor.light_physical_state: uncertain on every light fault; otherwise the last verified dimmer meaning;
- sensor.boot_dim_off_verified_ms: elapsed boot time to the first verified 0 % target;
- binary_sensor.light_fault and sensor.light_fault_reason;
- indexed target/readback and step entities inherited from Test 08.

Every indexed light step republishes the target percentage, expected W2/W1, readback W2/W1, coil state, SHDN
state, fault state, and last verified logical light state before publishing the unique step marker.

## Test procedure

1. Compile with SKETCH=sketches/system-tests/08b_RelayBypassDimmerDiagnosticTest. Confirm credential cleanup,
   minimal SHT/RTC/tach ISRs, no D4 LOW write, and no shutdown call in boot, normal, error, recovery, or OTA paths.
2. Cold-start only the already powered controller and AD5263. Connect lamp mains last and confirm the verified
   boot 0 % snapshot.
3. With relay contacts bypassed, command 0, 25, 50, 75, 100, and 0 %. Require exact RDAC readback,
   the expected lamp response, continuously released SHDN, and the documented relay-coil sequence.
4. Execute at least twenty 0 -> 100 -> 0 cycles. Reject availability loss, I2C errors, or OTA poll gaps.
5. Exercise the available AD5263 fault paths. No fault may assert SHDN or claim that the lamp is off.
6. Supervise a warm MCU restart and OTA with the lamp plugged in. Record the expected short full-brightness
   state caused by the external SHDN pull-down. Keep manual mains disconnection immediately available. Require
   subsequent 0 % write/readback, HA reconnect, and working OTA.
7. Disable WiFi while holding a stable brightness. The RDAC state and all inherited local Test 08 functions must
   continue. Restore WiFi and confirm complete HA republishing.
8. Run at least a 30-minute soak with repeated light changes and finish with OTA.
9. Interpret the result:
   - a stable bypass run shifts suspicion toward relay-contact mains switching, inrush, or lamp-driver
     disturbance;
   - another failure with bypassed contacts shifts investigation toward the relay coil, I2C/dimmer path, power
     integrity, or firmware.

Stop immediately on uncontrolled brightness, unexpected D4 LOW, abnormal sound or smell, controller reset,
loss of low-voltage module power, or any situation where manual mains disconnection is not immediately possible.

## Acceptance state

Status: implemented and compile-verified; supervised installed-system validation pending.

No raw Home Assistant history or real measurement export belongs in the repository.
