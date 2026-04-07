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
- AD5263BRUZ50 (auf TSOPP24-Adapter) als digital gesteuerter Widerstand
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

Der Licht-Dimmer wird als **digital einstellbarer Widerstand** zwischen `Dim+` und `Dim-` umgesetzt.
Es gibt im Lichtsystem weiterhin zwei getrennte Pfade:

- AD5263-Dimmerpfad (analoger Widerstandspfad)
- hartes Abschalten über Relais

Verbindliche AD5263-Hardware:

- Bauteil: `AD5263BRUZ50`
- Montage über TSOPP24-Adapter
- `VDD = 12 V`
- `VSS = GND`
- `VL/VLOGIC = 3,3 V`
- Abblockung `VDD` gegen `VSS`: `100 nF` oder `1 µF`
- Abblockung `VL` gegen `GND`: `100 nF`
- keine galvanische Trennung im Dimmerpfad

Verbindlicher I²C-Betrieb:

- `DIS = 1` (I²C-Modus)
- `SDI/SDA` an SDA
- `CLK/SCL` an SCL
- `CS/AD0` und `RES/AD1` als Adressbits
- `AD0 = GND`, `AD1 = GND`
- feste 7-Bit-Adresse: `0x2C`
- Pull-ups der I²C-Leitungen auf `3,3 V`

Verbindliche analoge Kanalverschaltung:

- Verwendet werden Kanal 1 und Kanal 2
- Signalpfad: `Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-`

Helligkeitsmapping (Firmware-Konzept):

- Firmware arbeitet logisch mit `0..100 %` Helligkeit
- Mapping auf RDAC-Werte ist verbindlich:
  - `0 % = W2 255, W1 0`
  - `50 % = W2 0, W1 0`
  - `100 % = W2 0, W1 255`
- `0..50 %`: zuerst Kanal 2 herunterregeln
- `50..100 %`: danach Kanal 1 hochregeln

Widerstandsgrenzen:

- Zielbereich am Lichteingang nominell ca. `0..100 kΩ`
- praktisch mit zwei AD5263-Kanälen im Rheostat-Betrieb angenähert
- konkrete untere/obere Limits werden später über Firmware-Konstanten begrenzt
- diese Limits sind **nicht** über HA konfigurierbar

### 3. SHDN und Schaltreihenfolge

`SHDN` wird aktiv verwendet und liegt auf Pin `4`.
Verbindlich vorgesehen ist ein externer `10 kΩ` Pull-down nach GND.
Durch diesen externen Pull-down wird kein interner Pull-up für `SHDN` verwendet.

Wichtig:

- Der AD5263 startet bei Power-on auf Midscale.
- Ohne Sequenzierung kann das Licht beim Zuschalten mit falscher Helligkeit starten.

Verbindliche Startreihenfolge:

1. AD5263 bleibt beim Boot zunächst in `SHDN`
2. Konfiguration laden
3. Sollzustand rekonstruieren
4. Widerstandswert setzen
5. `SHDN` freigeben
6. erst danach Relais schließen

Verbindliche Ausschaltreihenfolge:

1. Relais öffnen
2. danach AD5263 in `SHDN`

### 4. Fehlerverhalten Lichtpfad

- Wenn AD5263 beim Boot nicht erreichbar ist: Relais bleibt offen, Licht bleibt aus, `light_fault` und `light_fault_reason` werden gesetzt.
- Wenn AD5263 im Betrieb ausfällt oder ein Schreib-/Readback-Fehler erkannt wird: Relais öffnen, `light_fault` setzen, `light_fault_reason` aktualisieren.
- Typische Werte für `light_fault_reason`:
  - `ad5263_not_found`
  - `ad5263_write_failed`
  - `ad5263_readback_mismatch`

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

## Home Assistant

Die MQTT-/HA-Integration basiert auf:

- Arduino Home Assistant Integration von Dawid Chyrzynski
  https://github.com/dawidchyrzynski/arduino-home-assistant

Weitere Details stehen in `entity-model.md`.

## Offene Probleme

- Die finale Firmware-Implementierung muss noch vollständig gegen diese AD5263-Spezifikation verifiziert werden.
- Der reale Aufbau muss hardwareseitig validiert werden, insbesondere Mapping, Grenzwerte, SHDN-Verhalten und Fault-Reaktion.

## Roadmap

1. AD5263-Mapping und Dimmergrenzen im realen Aufbau verifizieren
2. Resume-State- und Fault-Strategie in der Firmware vollständig nachziehen
3. End-to-End-Tests für Boot-Sequenz, Restart-Recovery und Fehlerpfade durchführen
