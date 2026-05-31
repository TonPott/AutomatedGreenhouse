# Grow Controller (Arduino Nano 33 IoT)

This repository contains the firmware and project documentation for a grow controller based on an **Arduino Nano 33 IoT**.

The controller manages and monitors:

- temperature and humidity via an **SHT3x**
- a **12V 3-pin fan** with tachometer evaluation
- a **dimmable grow light** via AD5263 dimmer + relay
- a **capacitive soil moisture sensor**
- a **DS3231 RTC with AT24C32 EEPROM**
- **Home Assistant** integration via **MQTT** using **ArduinoHA**

The system should work with Home Assistant and also continue operating locally and autonomously if the connection is lost.

## Project Structure

```text
.
|-- AGENTS.md
|-- README.md
|-- SPEC.md
|-- MODULES.md
|-- HARDWARE.md
|-- ROADMAP.md
|-- DECISIONS.md
|-- libraries.txt
|-- arduino-cli.yaml
|-- sketches/
|   `-- Smaeenhouse/
|       |-- Smaeenhouse.ino
|       |-- sketch.yaml
|       |-- Credentials.example.h
|       `-- *.h / *.cpp
|-- hardware-tests/
|   |-- README.md
|   `-- <test-sketch>/
|-- scripts/
|   |-- setup-arduino.*
|   |-- check-arduino.*
|   |-- cleanup-worktree.*
|   `-- cleanup-arduino-toolchain.*
`-- .github/workflows/arduino-compile.yml
```

The project remains Arduino-IDE-compatible. Open `sketches/Smaeenhouse/Smaeenhouse.ino` in the Arduino IDE.

## Project Overview For New Users

Recommended reading order:

1. Review the **schematic in the project folder** (`FullArduinoHouse.fzz`)
2. Read `SPEC.md` for functional requirements
3. Read `HARDWARE.md` for board, wiring, signal levels, and safety notes
4. Read `MODULES.md` for technical structure
5. Read `docs/entity-model.md` for Home Assistant entities
6. Read `ROADMAP.md` for open validation and follow-up work
7. Read `DECISIONS.md` for design rationale and major decisions
8. Read `libraries.txt` for required Arduino libraries

The schematic in the project folder is the primary wiring reference. Text documentation adds explanatory context for review and maintenance.

## Setup

Arduino CLI setup is a one-time preparation per local machine. The setup scripts may update Arduino indexes, install the board core, and prepare libraries.

Windows:

```powershell
.\scripts\setup-arduino.ps1
```

Linux / Codex Cloud / GitHub Actions:

```bash
./scripts/setup-arduino.sh
```

By default, all worktrees of this repository use a shared Arduino home:

- Windows: `%LOCALAPPDATA%\ArduinoCodex\<repo-name>\arduino-cli`
- Linux/macOS: `$HOME/.cache/arduino-codex/<repo-name>/arduino-cli`

Set `ARDUINO_PROJECT_HOME` to use a different toolchain location.

## Compile Check

Compile checks do not install anything. They generate an ignored local Arduino CLI configuration under `.local/arduino-cli.yaml`, use the shared toolchain, and write build output to `.build/`.

Windows:

```powershell
.\scripts\check-arduino.ps1
```

Linux / Codex Cloud / GitHub Actions:

```bash
./scripts/check-arduino.sh
```

The default sketch is `sketches/Smaeenhouse`. The default profile is `nano33iot`.

Hardware test example:

```powershell
$env:SKETCH = "hardware-tests/AD5263Test"
.\scripts\check-arduino.ps1
```

```bash
SKETCH=hardware-tests/AD5263Test ./scripts/check-arduino.sh
```

If the compile check reports that the Arduino toolchain is not prepared, run the matching setup script once and retry.

## Version Policy

This project pins core and library versions deliberately because the production firmware depends on specific board, networking, RTC, sensor, persistence, and Home Assistant APIs.

Keep these files in sync whenever dependencies change:

- `libraries.txt`
- `sketches/Smaeenhouse/sketch.yaml`
- `scripts/setup-arduino.ps1`
- `scripts/setup-arduino.sh`

Normal compile checks must not install missing dependencies. Run setup first when a pinned core or library version changes, then run the compile check.

## Credentials

Create a local `sketches/Smaeenhouse/Credentials.h` file based on `sketches/Smaeenhouse/Credentials.example.h`.

**Important:** `Credentials.h` must not be committed to the repository. The check scripts temporarily copy `Credentials.example.h` only when no real credentials file exists, and remove that temporary file after compilation.

For hardware-test compile checks that include credential-dependent modules, the scripts may temporarily copy the production example credentials into the selected test sketch folder. Real hardware uploads should use real local credentials, never committed credentials.

## Cleanup

Worktree-local generated files can be removed with:

```powershell
.\scripts\cleanup-worktree.ps1
```

```bash
./scripts/cleanup-worktree.sh
```

These scripts remove `.build/`, `.arduino-cache/`, `.local/`, and `.arduino/` in the current worktree. They do not delete the shared Arduino toolchain.

The shared toolchain is intentionally long-lived. Manual project-end cleanup uses separate scripts that require confirmation:

```powershell
.\scripts\cleanup-arduino-toolchain.ps1
```

```bash
./scripts/cleanup-arduino-toolchain.sh
```

## Important Hardware Notes

- The RTC module provides a **DS3231** RTC and **AT24C32** EEPROM. The firmware uses RTClib for the RTC and JC_EEPROM for the external EEPROM.
- The grow light is dimmed through an `AD5263BRUZ50` as a digitally adjustable resistance between `Dim+` and `Dim-`; the relay remains responsible for hard switching the mains supply.
- The fan tach signal is converted to 3.3 V logic through a **2N3904 transistor stage**. See `HARDWARE.md` for the exact signal-conditioning notes.
- The **SQW/INT output** of the DS3231 is used for Arduino-internal light schedule alarms.

## Home Assistant

The MQTT/HA integration is based on the Arduino Home Assistant Integration by Dawid Chyrzynski:

https://github.com/dawidchyrzynski/arduino-home-assistant

Further details are documented in `docs/entity-model.md`.

## Codex Workflow

- Open the repository root in the Codex app, not the sketch folder.
- Keep changes small and reviewable.
- After code or build configuration changes, run `scripts/check-arduino`.
- After documentation-only changes, a diff review is sufficient.
- Do not commit generated build artifacts.
- Run hardware tests and uploads to real boards locally.

Recommended local Codex actions:

```text
Arduino Setup   -> .\scripts\setup-arduino.ps1
Arduino Compile -> .\scripts\check-arduino.ps1
```

Current open tasks and validation steps are tracked in `ROADMAP.md`. Important design decisions are recorded in `DECISIONS.md`.
