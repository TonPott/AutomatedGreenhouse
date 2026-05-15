# Grow Controller (Arduino Nano 33 IoT)

Dieses Repository enthält die Firmware und die Projektdokumentation für einen Grow-Controller auf Basis eines **Arduino Nano 33 IoT**.

## Ziel

Der Controller steuert und überwacht:

- Temperatur und Luftfeuchtigkeit über einen **SHT3x**
- einen **12V 3-Pin-Lüfter** mit Tachoauswertung
- ein **dimmbares Grow-Light** über AD5263-Dimmer + Relais
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
  - Dimmung über einen variablen Widerstand zwischen `Dim+` und `Dim-`
- AD5263BRUZ50 (auf TSSOP-24-Adapter) als digital gesteuerter Widerstand
- Home Assistant mit MQTT

## Projektüberblick für neue Nutzer

Wenn du das Projekt zum ersten Mal siehst, beginne am besten in dieser Reihenfolge:

1. **Schaltplan im Projektordner** ansehen
2. `SPEC.md` lesen – fachliche Anforderungen
3. `MODULES.md` lesen – technische Struktur
4. `docs/entity-model.md` lesen – Home-Assistant-Entities
5. `libraries.txt` lesen – benötigte Bibliotheken

Der Schaltplan im Projektordner ist die Primärreferenz für die Verdrahtung.
Diese README ergänzt den Schaltplan um kurze textliche Erklärungen.

## Pinbelegung

- SHT Alert: `7`
- RTC SQW/INT: `10`
- Fan Switch: `2`
- Fan Tach: `A1`
- Light Power Relay: `3`
- Light Dim SHDN: `4`
- Light Dimmer (AD5263): `I²C` über `SDA/SCL`
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

### 2. Licht-Dimmer (AD5263BRUZ50)

Der alte PWM-/RC-/PC817-Dimmerpfad ist in diesem Branch nicht mehr das Zielkonzept. Das Grow-Light wird stattdessen über einen `AD5263BRUZ50` als digital einstellbarer Widerstand zwischen `Dim+` und `Dim-` gedimmt.

Hardware-Kurzfassung:

- `AD5263BRUZ50` auf TSSOP-24-Adapter
- zwei AD5263-Kanäle in Reihe für näherungsweise `0..100 kΩ`
- niedriger effektiver Widerstand = niedrige Helligkeit, hoher effektiver Widerstand = hohe Helligkeit
- keine galvanische Trennung im Dimmerpfad
- hartes Abschalten der 230-V-Zuleitung bleibt Aufgabe des Relais

Pin-Kurzfassung:

- Light Power Relay: Pin `3`
- Light Dim `SHDN`: Pin `4` mit externem `10 kΩ` Pull-down
- Light Dimmer: I²C über `SDA/SCL`, Adresse `0x2C`

Die exakte AD5263-Verschaltung, das RDAC-Mapping, die Boot-Sequenz und die Fault-/Readback-Strategie stehen in `SPEC.md` und `MODULES.md`.

### 3. Lüfter-Tacho-Signalaufbereitung

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
- `docs/entity-model.md` – Home-Assistant-Entity-Modell
- `Credentials.example.h` – Vorlage für die lokalen Zugangsdaten

## Nutzung mit Arduino IDE

Das Projekt ist bewusst als klassischer Arduino-Sketchordner gedacht:

- `Smaeenhouse/Smaeenhouse.ino`
- alle `.h/.cpp` im Ordner `Smaeenhouse/`
- keine PlatformIO-Pflicht

Unter `module-sketches/` liegen zusätzliche Test-Sketche, mit denen sich einzelne Module getrennt auf dem Board prüfen lassen.

## Credentials

Lege lokal eine Datei `Credentials.h` an, basierend auf `Credentials.example.h`.

**Wichtig:** `Credentials.h` darf nicht ins Repository committed werden.

## Persistenz

Persistente Konfiguration wird im **AT24C32-EEPROM** des RTC-Moduls gespeichert.
Der Zugriff erfolgt über die Bibliothek **JC_EEPROM**.

Zusätzlich wird ein kleiner `Light Resume State` vorgesehen, um den Sollzustand nach Neustarts konsistent zu rekonstruieren.

## Zeitsynchronisation und Lichtzeitplan

Die Zeit wird per NTP synchronisiert:

- beim Boot
- danach mindestens einmal pro Tag

Danach dient die **DS3231** als lokale Zeitbasis.

Der Arduino-interne Lichtzeitplan wird in die beiden DS3231-Alarmregister gespiegelt.
Die Alarmereignisse werden über den SQW/INT-Ausgang an den Arduino gemeldet.

## Bodenfeuchte-Kalibrierung

Die Firmware verwaltet keinen Kalibrierungsassistenten und keine interne Kalibrierungs-State-Machine. Home Assistant orchestriert den Ablauf.

Die Firmware stellt dafür nur diese Schnittstellen bereit:

- periodisches Lesen des Bodenfeuchte-Rohwerts
- periodische Berechnung der Bodenfeuchte in Prozent
- `sensor.soil_moisture_raw`
- `sensor.soil_moisture_percent`
- `button.read_soil_raw_value`
- persistente Konfigurationswerte `number.soil_air`, `number.soil_water` und `number.soil_depth_mm`

Für interne ADC-Rohwerte darf die Firmware defensiv den 12-bit-Bereich `0..4095` verwenden. Die persistenten HA-Kalibrierwerte `soil_air` und `soil_water` bleiben davon getrennt im erwarteten Projektbereich `0..1000` mit Schrittweite `1`.

Der vorgesehene Ablauf ist:

1. Für den Luftwert liegt der Sensor trocken in Luft. HA ruft `button.read_soil_raw_value` auf, wartet auf den aktualisierten Wert von `sensor.soil_moisture_raw` und schreibt ihn nach `number.soil_air`.
2. Für den Wasserwert liegt der Sensor bei der Referenztiefe von `120 mm` in Wasser. HA ruft wieder `button.read_soil_raw_value` auf, wartet auf `sensor.soil_moisture_raw` und schreibt ihn nach `number.soil_water`.
3. Die reale Einstecktiefe im Substrat wird manuell in `number.soil_depth_mm` eingetragen.

Separate Firmware-Buttons wie `capture_soil_air` oder `capture_soil_water` sollen nicht ergänzt werden.

`soil_depth_mm` ist ein aktiver Korrekturparameter. Die Firmware verwendet eine lineare Tiefenkorrektur mit `SOIL_REFERENCE_DEPTH_MM = 120`:

```text
depth_factor = soil_depth_mm / SOIL_REFERENCE_DEPTH_MM
percent = (soil_air - raw) / ((soil_air - soil_water) * depth_factor) * 100
```

Der Luftreferenzwert entspricht `0 %`, der Wasserreferenzwert bei `120 mm` entspricht `100 %`. Gültige Ergebnisse werden auf `0..100 %` begrenzt. Wenn `soil_depth_mm < 20`, ist der Prozentwert ungültig/unavailable; der Rohwert darf weiter publiziert werden.

## Home Assistant

Die MQTT-/HA-Integration basiert auf:

- Arduino Home Assistant Integration von Dawid Chyrzynski
  https://github.com/dawidchyrzynski/arduino-home-assistant

Weitere Details stehen in `docs/entity-model.md`.

## Offene Probleme

- Die finale Firmware-Implementierung muss noch vollständig gegen diese AD5263-Spezifikation verifiziert werden.
- Der reale Aufbau muss hardwareseitig validiert werden, insbesondere Mapping, Grenzwerte, SHDN-Verhalten und Fault-Reaktion.
- HA-Scripts/Automationen für den dokumentierten Bodenfeuchte-Kalibrierungsablauf müssen noch im Home-Assistant-Setup umgesetzt werden.

## Roadmap

1. AD5263-Mapping und Dimmergrenzen im realen Aufbau verifizieren
2. Resume-State- und Fault-Strategie in der Firmware vollständig nachziehen
3. HA-Automationen für `button.read_soil_raw_value` und das Schreiben nach `number.soil_air` / `number.soil_water` ergänzen
4. End-to-End-Tests für Boot-Sequenz, Restart-Recovery und Fehlerpfade durchführen
