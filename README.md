# Grow Controller (Arduino Nano 33 IoT)

Dieses Repository enthält die Firmware und die Projektdokumentation für einen Grow-Controller auf Basis eines **Arduino Nano 33 IoT**.
Entstanden in enger Zusammenarbeit mit ChatGPT 5.4 und Codex.

## Ziel

Der Controller steuert und überwacht:

- Temperatur und Luftfeuchtigkeit über einen **SHT3x**
- einen **12V 3-Pin-Lüfter** mit Tachoauswertung
- ein **dimmbares Grow-Light** über PWM + Relais
- einen **kapazitiven Bodenfeuchtesensor**
- eine **DS3231-RTC mit AT24C32-EEPROM**
- die Integration in **Home Assistant** per **MQTT** über **ArduinoHA**

Das System soll sowohl mit Home Assistant als auch bei Verbindungsverlust lokal autonom funktionieren.

## Hardware

- Arduino Nano 33 IoT
- Temperature and Humidity Sensor CQrobot CQRSHT31FA (Sensirion SHT31-DIS-F, I²C + Alert-Pin)
- 3-Pin 12V PC-Fan
- WINGONEER Tiny DS3231 AT24C32 I2C Modul
  - RTC: **DS3231**
  - externes EEPROM: **AT24C32**
  - SQW/INT-Ausgang für RTC-Alarme
- DFRobot Capacitive Soil Moisture Sensor SEN0308
- ViparSpectra P1000 Grow-Light
  - 230-V-Zuleitung über 3,3-V-Relaismodul geschaltet
  - Dimmung über Optokoppler **PC817** an einem 0–10-V-Stromeingang
- Home Assistant mit MQTT

## Projektüberblick für neue Nutzer

Wenn du das Projekt zum ersten Mal siehst, beginne am besten in dieser Reihenfolge:

1. **Schaltplan im Projektordner** ansehen
2. `SPEC.md` lesen – fachliche Anforderungen
3. `MODULES.md` lesen – technische Struktur
4. `entity-model.md` lesen – Home-Assistant-Entities
5. `libraries.txt` lesen – benötigte Bibliotheken

Der Schaltplan im Projektordner ist die Primärreferenz für die Verdrahtung.
Diese README ergänzt den Schaltplan um kurze textliche Erklärungen.

## Pinbelegung

- SHT Alert: `7`
- RTC SQW/INT: `10`
- Fan Switch: `2`
- Fan Tach: `A1`
- Light PWM: `4`
- Light Power Relay: `3`
- Soil Moisture: `A0`

## Wichtige Hardware-Hinweise

### 1. RTC + EEPROM

Das RTC-Modul stellt zwei Funktionen bereit:

- **DS3231** als präzise RTC
- **AT24C32** als externes I²C-EEPROM für persistente Konfiguration

Die Firmware nutzt:

- RTClib für die DS3231
- **JC_EEPROM** für den Zugriff auf das AT24C32-EEPROM (benötigt **Streaming** https://github.com/janelia-arduino/Streaming)

Zusätzlich wird der **SQW/INT-Ausgang** der DS3231 für den Arduino-internen Lichtzeitplan genutzt.

### 2. RTC-Alarmleitung

- DS3231 `SQW/INT` an Arduino-Pin `10`
- Arduino-Seite mit `INPUT_PULLUP`
- falls der reale Aufbau störanfällig ist, kann zusätzlich ein externer 10-kΩ-Pull-up auf 3,3 V verwendet werden

### 3. Licht-Dimmer (RC + PC817)

Der PWM-Ausgang wird über ein RC-/Optokoppler-Netzwerk geglättet bzw. an den Lichteingang angepasst:

- `PIN_LIGHT_PWM` → 1 kΩ → Knoten
- Knoten → 0,1 µF gegen GND
- Knoten → 330 Ω → PC817 Eingang (+)
- PC817 Eingang (−) → GND

### 4. Hartes Licht-Aus

Die 230-V-Zuleitung des Licht-Netzteils wird über ein **3,3-V-Relaismodul mit Optokoppler** geschaltet.
Im Code bleibt das konzeptionell ein separater „hard power off“-Pfad.

### 5. Lüfter-Tacho-Signalaufbereitung

Das Lüfter-Tachosignal wird über eine **2N3904-Transistorstufe** auf 3,3-V-Logik für den Nano 33 IoT umgesetzt:

- Lüfter-Tacho mit 10 kΩ auf 9 V pull-up
- Tacho über 47 kΩ an die Basis eines 2N3904
- 100 kΩ von Basis nach GND
- Emitter an GND
- Collector an `A1`
- 10 kΩ Pull-up vom Collector auf 3,3 V

Das resultierende Signal ist invertiert; die Firmware berücksichtigt das über die gewählte Interrupt-Flanke.

## Wichtige Bibliotheken

Siehe `libraries.txt`.

## Wichtige Doku-Dateien

- `SPEC.md` – fachliche Anforderungsliste
- `MODULES.md` – technische Modulspezifikation
- `AGENTS.md` – Arbeitsregeln für Codex / KI-Agenten
- `entity-model.md` – Home-Assistant-Entity-Modell
- `Credentials.example.h` – Vorlage für die lokalen Zugangsdaten

## Nutzung mit Arduino IDE

Das Projekt ist bewusst als klassischer Arduino-Sketchordner gedacht:

- `Smaeenhouse.ino`
- alle `.h/.cpp` im gleichen Ordner
- keine PlatformIO-Pflicht

Unter `module-sketches/` liegen zusätzliche Test-Sketche, mit denen sich einzelne
Module getrennt auf dem Board prüfen lassen.

## Credentials

Lege lokal eine Datei `Credentials.h` an, basierend auf `Credentials.example.h`.

**Wichtig:** `Credentials.h` darf nicht ins Repository committed werden.

## Persistenz

Persistente Konfiguration wird im **AT24C32-EEPROM** des RTC-Moduls gespeichert.
Der Zugriff erfolgt über die Bibliothek **JC_EEPROM**.

## Zeitsynchronisation und Lichtzeitplan

Die Zeit wird per NTP synchronisiert:

- beim Boot
- danach mindestens einmal pro Tag

Danach dient die **DS3231** als lokale Zeitbasis.

Der Arduino-interne Lichtzeitplan wird in die beiden DS3231-Alarmregister gespiegelt.
Die Alarmereignisse werden über den SQW/INT-Ausgang an den Arduino gemeldet.

## Home Assistant

Die MQTT-/HA-Integration basiert auf:

- Arduino Home Assistant Integration von Dawid Chyrzynski
  https://github.com/dawidchyrzynski/arduino-home-assistant

Weitere Details stehen in `entity-model.md`.

## Offene Probleme

- **Licht-Dimmung über den Sosen-Treiber ist noch nicht final gelöst.** Der dokumentierte RC-/PC817-Ansatz bildet den erwarteten Widerstands-/Stromsenken-Eingang des Treibers bisher nicht zuverlässig nach.
- **Die endgültige Hardwarestrategie für die Dimmung ist offen.** Wahrscheinlich wird ein Widerstands-Stufennetzwerk oder eine andere robuste, zum Treiber passende Lösung nötig.
- **Das Home-Assistant-Frontend ist noch nicht spezifiziert und umgesetzt.** Zeitdarstellung, Kalibrierungsdialog und bedingte Sichtbarkeit im Dashboard fehlen noch als sauberes Konzept.
- **Die von Codex erzeugte Firmware muss weiter gegen die aktuelle Dokumentation geprüft werden.** Besonders wichtig bleiben RTC-Alarm-Logik, Persistenz, HA-Entities und das Zusammenspiel zwischen Arduino-Auto-Mode und HA-Steuerung.
- **Der reale Aufbau muss weiter hardwareseitig validiert werden.** Dazu gehören insbesondere Dimmverhalten, Versorgungskonzept und das Verhalten im Offline-Fallback.

## Roadmap

1. **Dimmkonzept finalisieren**
   - den Eingang des Sosen-Treibers mit einer robusten Lösung ansteuern
   - dokumentieren, welche Hardwarevariante endgültig verwendet wird

2. **Firmware gegen die aktuelle Spezifikation nachziehen**
   - Codex gezielt gegen `SPEC.md`, `MODULES.md` und `entity-model.md` patchen lassen
   - bestehende Inkonsistenzen im Code beseitigen

3. **Home-Assistant-Frontend-Konzept ergänzen**
   - Zeitdarstellung für `light_on_time` / `light_off_time`
   - Kalibrierungsablauf für den Bodenfeuchtesensor
   - Dashboard-Layout mit bedingter Sichtbarkeit je nach Modus und Bedienbarkeit

4. **HA-Dashboard und Automationen umsetzen**
   - Helpers, Templates, Scripts und Karten definieren
   - Scheduler-Integration für den HA-Dimmauftrag vervollständigen

5. **Gesamtsystem im realen Aufbau testen**
   - Schaltverhalten, Sensorik, RPM, RTC-Alarme und Persistenz prüfen
   - Verhalten bei WLAN-/MQTT-Ausfall validieren

6. **Dokumentation und Code synchron halten**
   - nach jeder größeren Hardware- oder Logikänderung zuerst die Anforderungen aktualisieren
   - anschließend Codex per Patch-/Review-Workflow auf den bestehenden Code ansetzen
