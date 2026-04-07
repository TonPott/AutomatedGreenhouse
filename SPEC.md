## Anforderungsliste v5

## 1. Ziel des Projekts

Das Arduino Nano 33 IoT steuert ein Grow-Setup mit:

- SHT3x Temperatur-/Luftfeuchtesensor
- 12V-Lüfter mit Tachosignal
- dimmbarem Grow-Light
- RTC_DS3231 + externem AT24C32-EEPROM
- kapazitivem Bodenfeuchtesensor
- Home-Assistant-Integration über MQTT

Das System soll mit Home Assistant zusammenarbeiten, bei Verbindungsverlust aber lokal autonom weiterlaufen können.

## 2. Hardware

### 2.1 Mikrocontroller
- Arduino Nano 33 IoT

### 2.2 Sensoren und Aktoren
- SHT3x per I²C mit Alert-Pin
- 3-Pin 12V PC-Lüfter
- WINGONEER Tiny DS3231 AT24C32 I2C Modul
  - RTC per I²C mit RTClib, Typ `RTC_DS3231`
  - externes I²C-EEPROM `AT24C32`
  - SQW/INT-Ausgang für RTC-Alarme
- Grow-Light:
  - Dimmung über einen digital einstellbaren Widerstand (AD5263) zwischen `Dim+` und `Dim-`
  - harte Abschaltung über 3,3-V-Relaismodul
- DFRobot Capacitive Soil Moisture Sensor SEN0308
- WLAN + MQTT zu Home Assistant

### 2.3 Dokumentationsanforderung Hardware
Die Projektdokumentation soll auch für Nutzer verständlich sein, die das Projekt zum ersten Mal sehen.
Der Schaltplan im Projektordner ist die Primärreferenz und soll in den Textdokumenten ausdrücklich erwähnt werden.

## 3. Pinbelegung

- SHT Alert: Pin 7
- RTC Alarm (SQW/INT): Pin 10
- Fan Switch: Pin 2
- Fan Tach: Pin A1
- Light Power Relay: Pin 3
- Light Dim SHDN: Pin 4
- Light Dimmer: I²C (SDA/SCL, AD5263 @ 0x2C)
- Soil Moisture: Pin A0

## 4. Temperatur / Luftfeuchtigkeit / SHT3x
### 4.1 Messung
Der SHT3x misst periodisch Temperatur und Luftfeuchtigkeit.

Diese Werte sollen:
- lokal nutzbar sein
- an Home Assistant übertragen werden

### 4.2 Thresholds
Es gibt konfigurierbare Schwellwerte:

- `TEMP_HIGH_SET`
- `TEMP_HIGH_CLEAR`
- `TEMP_LOW_SET`
- `TEMP_LOW_CLEAR`
- `HUM_HIGH_SET`
- `HUM_HIGH_CLEAR`
- `HUM_LOW_SET`
- `HUM_LOW_CLEAR`

Diese Werte sollen:
- persistent im externen EEPROM gespeichert werden
- beim Start in den Sensor geschrieben werden
- aus HA veränderbar sein

### 4.3 Interrupt-Logik
Der SHT3x übernimmt die Grenzwertüberwachung.

Bei einem Alert auf Pin 7 wird das Statusregister decodiert. Relevant sind insbesondere:

- `alertTriggers[2]` = Feuchte-Tracking-Alert
- `alertTriggers[3]` = Temperatur-Tracking-Alert
- `alertTriggers[4]` = Sensor-Reset erkannt

### 4.4 Reinitialisierung nach Sensor-Reset
Wenn `alertTriggers[4]` gesetzt ist, soll das genutzt werden, um den Sensor neu zu initialisieren bzw. mindestens die konfigurierten Thresholds erneut in den Sensor zu schreiben.

### 4.5 Zuständigkeit
- Der FanController wertet **keine eigenen Thresholds** aus
- Maßgeblich ist die SHT3x-Alert-Logik

## 5. Lüfter

### 5.1 Automatikbetrieb
Der Lüfter wird anhand der SHT3x-Alert-Logik gesteuert:

- relevante Alerts aktiv → Lüfter an
- Clear-Zustand erreicht → Lüfter aus

### 5.2 Manueller Betrieb
In HA soll der Lüfter manuell ein- und ausgeschaltet werden können.

### 5.3 Auto-Mode
Es gibt einen Auto-Mode-Switch für den Lüfter:

- ON: Sensor-Interrupts steuern den Lüfter
- OFF: Sensor-Interrupts werden ignoriert, manuelle Steuerung bleibt aktiv

### 5.4 RPM
Der Lüfter hat 2 Pulse pro Umdrehung.

RPM:
- Messfenster 30 Sekunden
- nur messen/veröffentlichen, wenn der Lüfter laufen soll

### 5.5 Hardware-Signalaufbereitung Tacho
Die Dokumentation soll die verwendete 2N3904-Stufe enthalten:

- Tacho-Leitung mit 10 kΩ auf 9 V pull-up
- Tacho über 47 kΩ an die Basis eines 2N3904
- 100 kΩ von Basis nach GND
- Emitter an GND
- Collector an Arduino `PIN_FAN_TACH`
- 10 kΩ Pull-up vom Collector auf 3,3 V

Das resultierende Signal ist invertiert.

## 6. Grow-Light

### 6.1 Grundstruktur
Das Licht hat zwei getrennte Steuerpfade:

- AD5263-Dimmerpfad als variabler Widerstand zwischen `Dim+` und `Dim-`
- hartes Relais-Aus der 230-V-Zuleitung

### 6.2 Verbindliche AD5263-Hardware

- Bauteil: `AD5263BRUZ50`
- Montage über TSOPP24-Adapter
- Versorgung:
  - `VDD = 12 V`
  - `VSS = GND`
  - `VL/VLOGIC = 3,3 V`
- Abblockung:
  - `100 nF` oder `1 µF` zwischen `VDD` und `VSS`
  - `100 nF` zwischen `VL` und `GND`
- keine galvanische Trennung im Dimmerpfad

### 6.3 I²C-Betrieb und Adresse

- `DIS = 1` für I²C-Betrieb
- `SDI/SDA` = SDA
- `CLK/SCL` = SCL
- `CS/AD0` und `RES/AD1` sind die I²C-Adressbits
- `AD0 = GND`
- `AD1 = GND`
- feste 7-Bit-I²C-Adresse: `0x2C`
- Pull-ups der I²C-Leitungen auf `3,3 V`

### 6.4 Verbindliche analoge Kanalverschaltung

Verwendet werden ausschließlich Kanal 1 und Kanal 2 des AD5263.

Verbindlicher Signalpfad:

- `Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-`

### 6.5 Helligkeitsmapping auf RDAC-Werte

Die Firmware arbeitet logisch weiter mit `0..100 %` Helligkeit und mappt intern auf RDAC-Werte.

Verbindliche Punkte:

- `0 % = W2 255, W1 0`
- `50 % = W2 0, W1 0`
- `100 % = W2 0, W1 255`

Verbindliche Zwischenlogik:

- von `0 %` bis `50 %` wird zuerst Kanal 2 heruntergeregelt
- von `50 %` bis `100 %` wird danach Kanal 1 hochgeregelt

### 6.6 Widerstandsgrenzen

- Zielbereich am Lichteingang: nominell ca. `0..100 kΩ`
- praktisch durch den AD5263BRUZ50 mit zwei Kanälen im Rheostat-Betrieb angenähert
- tatsächlich genutzte untere/obere Limits werden als projektinterne Firmware-Konstanten gesetzt
- diese Dimmer-Grenzwerte sind **nicht** über HA konfigurierbar

### 6.7 Zwei Steuerwelten
Es muss sauber unterschieden werden zwischen:

- **Arduino Auto-Schedule**
- **HA-gesteuertem Betrieb** (manuell oder per HA-Schedule)

Diese beiden dürfen sich nicht gleichzeitig steuern.

### 6.8 Licht-Modi

#### Modus A: Arduino Auto-Mode ON
Wenn `light_auto_mode = ON`:

- der interne Arduino-Schedule ist aktiv
- Trigger vom HA-Schedule werden ignoriert
- manuelle Helligkeitsanpassung ist erlaubt, aber nur temporär innerhalb des aktuellen Zeitfensters
- beim nächsten Arduino-Schedule-Event wird der Wert wieder überschrieben
- der HA-Ein/Aus-Schalter für das Licht soll in diesem Modus keine Wirkung haben oder deaktiviert sein

#### Modus B: Arduino Auto-Mode OFF
Wenn `light_auto_mode = OFF`:

- der Arduino-Schedule ist inaktiv
- HA-Schedule-Trigger dürfen das Licht steuern
- manuelle HA-Befehle dürfen das Licht wie ein normales dimmbares Licht steuern
- Ein/Aus-Schalter und Brightness-Slider sind aktiv

### 6.9 Dimmverhalten
Es gibt **keine globale feste Dimmrampe mehr für alle Fälle**.

Stattdessen gilt:

- für Arduino-Schedule-Fahrten wird die konfigurierte Auto-Dimmdauer verwendet
- für HA-Schedule-Befehle wird die vom Befehl mitgelieferte Dauer verwendet
- für rein manuelle Slider-/Schalter-Bedienung im HA-Modus darf das Licht wie ein normales dimmbares Licht unmittelbar reagieren

### 6.10 Hard Power Off
Es gibt zusätzlich einen separaten HA-Schalter/Befehl für hartes Ausschalten.

Dieser soll:

- nur das Relais sofort schalten
- den internen Dimmerzustand beibehalten

Dieser Befehl ist nur im HA-gesteuerten Betrieb relevant.

### 6.11 SHDN-Nutzung

- `SHDN` wird aktiv genutzt und dafür Pin 4 verwendet
- an `SHDN` ist verbindlich ein externer `10 kΩ` Pull-down nach GND vorzusehen
- wegen dieses externen Pull-downs wird kein interner Pull-up für `SHDN` verwendet
- der AD5263 soll während Reset/Boot standardmäßig im Shutdown bleiben

### 6.12 Start- und Ausschaltreihenfolge

Wichtig:

- der AD5263 startet bei Power-on auf Midscale
- ohne Sequenz entsteht beim Relais-Zuschalten sonst ein falscher Helligkeitsstart

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

### 6.13 Race-Condition- und Logikregeln fürs Licht
1. Es gibt immer genau **eine aktive Lichtsteuerquelle**:
   - Arduino Auto
   - oder HA

2. Bei `light_auto_mode = ON` werden HA-Schedule-Trigger ignoriert.

3. Bei `light_auto_mode = OFF` werden Arduino-Schedule-Events ignoriert.

4. Ein neuer gültiger Dimmauftrag ersetzt einen noch laufenden alten Dimmauftrag der aktiven Steuerquelle.

5. Beim Moduswechsel wird ein laufender Dimmauftrag beendet; die neue Steuerquelle übernimmt erst mit ihrem nächsten gültigen Befehl/Event.

### 6.14 Fehlerstrategie Lichtpfad

- Wenn AD5263 beim Boot nicht erreichbar ist:
  - Relais bleibt offen
  - Licht bleibt aus
  - `light_fault = true`
  - `light_fault_reason = ad5263_not_found`

- Wenn AD5263 im Betrieb ausfällt oder ein Schreib-/Readback-Fehler erkannt wird:
  - Relais öffnen
  - `light_fault = true`
  - `light_fault_reason` entsprechend setzen

- Vorgesehene Textwerte für `light_fault_reason`:
  - `ad5263_not_found`
  - `ad5263_write_failed`
  - `ad5263_readback_mismatch`

## 7. Licht-Zeitplan über DS3231-Alarme
### 7.1 Arduino-interner Zeitplan
Der Arduino speichert intern:

- Einschalt-/Dim-On-Zeit
- Ausschalt-/Dim-Off-Zeit
- Standard-Dimmdauer

### 7.2 Zeitformat
Intern:
- minutes since midnight

### 7.3 RTC-Alarmnutzung
Der Arduino-interne Zeitplan soll über die **zwei Alarmregister der DS3231** umgesetzt werden.

Anforderungen:
- die gespeicherten Ein-/Aus-Zeiten werden in die DS3231-Alarmregister geschrieben
- bei einem Alarm wird über den `SQW/INT`-Pin ein Interrupt ausgelöst
- die ISR setzt nur ein Flag
- im Hauptloop wird ausgewertet, welcher Alarm ausgelöst wurde
- danach wird entsprechend ein On-/Off-Dimmauftrag gestartet

### 7.4 Bibliotheksanforderung
Es ist zu prüfen und in der Implementierung zu nutzen, dass RTClib für `RTC_DS3231` die nötigen Funktionen bereitstellt, insbesondere:

- `writeSqwPinMode(...)`
- `setAlarm1(...)`
- `setAlarm2(...)`
- `alarmFired(...)`
- `clearAlarm(...)`

### 7.5 Pull-up der Alarmleitung
Da `SQW/INT` Open-Drain ist, soll Arduino-seitig mindestens `INPUT_PULLUP` verwendet werden.
Falls die reale Leitung instabil ist, darf zusätzlich ein externer Pull-up auf 3,3 V vorgesehen werden.

### 7.6 Abgrenzung zu HA-Schedule
Punkt 7 gilt nur für den **Arduino-internen Zeitplan**.

Ein zusätzlicher HA-Schedule ist davon getrennt und nur wirksam, wenn `light_auto_mode = OFF`.

## 8. Bodenfeuchte

### 8.1 Messwerte
Verfügbar sein sollen:
- berechnete Bodenfeuchte in %
- Rohwert des Sensors

### 8.2 Kalibrierparameter
- Air value
- Water value
- Sensor depth

Diese Werte:
- sind persistent gespeichert
- werden in HA angezeigt
- können in HA geändert werden

### 8.3 Kalibrierung
Die Kalibrierroutine läuft vollständig in HA.

Daher braucht die Firmware nicht:
- den Kalibrierschritt zu kennen
- Messungen zu pausieren
- einen internen Kalibrierstatus
- einzelne Zwischenschritte zu speichern

Die Firmware muss nur:
- den Rohwert auf Anforderung liefern
- am Ende die finalen Kalibrierdaten übernehmen
- diese per Befehl in das externe EEPROM schreiben

### 8.4 HA-Bedienung
Es reicht ein Button:
- `read soil raw value`

### 8.5 Messintervall
Der Bodenfeuchtesensor soll:
- alle **10 Sekunden** ausgelesen werden
- an HA veröffentlicht werden

Sinnvoll ist, dabei sowohl:
- den Rohwert
- als auch den berechneten %-Wert

zu publizieren.

## 9. RTC / Zeit / EEPROM

### 9.1 RTC-Typ
- `RTC_DS3231`

### 9.2 Externes EEPROM
Die persistente Konfiguration soll nicht im Arduino-Flash, sondern im **AT24C32** des RTC-Moduls gespeichert werden.
Der Zugriff auf dieses EEPROM erfolgt über die Bibliothek **JC_EEPROM** (diese benötigt die Bibliothek **Streaming**).
Die I²C-Adresse des auf dem WINGONEER Tiny DS3231 AT24C32 I²C-Modul verbauten AT24C32 EEPROM ist im realen Aufbau verifiziert und in diesem Projekt fest vorgegeben.
Sie lautet 0x57.

Anforderungen:
- Die Firmware soll diese bekannte EEPROM-I²C-Adresse direkt verwenden.
- Die EEPROM-Adresse soll nicht zur Laufzeit erraten, gescannt oder automatisch gesucht werden.
- Die Adresse wird zentral in `Config.h` oder einer vergleichbaren Konfigurationsdatei definiert.
- Die Persistenzschicht verwendet diese feste Adresse für alle Lese-/Schreibzugriffe über `JC_EEPROM`.
- Die Bibliothek **JC_EEPROM** benötigt als Abhängigkeit die Bibliothek **Streaming**.

Begründung:
- Das EEPROM wurde im Testaufbau erfolgreich mit `JC_EEPROM` angesprochen.
- Damit entfällt unnötige Autodetektion und die Initialisierung bleibt einfacher und robuster.

### 9.3 Nutzung
Die RTC ist lokale Zeitbasis für:
- Arduino-Lichtschedule
- autonomen Betrieb bei Verbindungsverlust

Die RTC-Alarme sind die Triggerquelle für den Arduino-internen Lichtplan.

### 9.4 Synchronisation
Die Uhrzeit soll synchronisiert werden:
- beim Boot
- danach mindestens 1× pro Tag

Optional zusätzlich:
- manueller Sync per HA-Button

### 9.5 NTP
Die Firmware soll aktiv per NTP synchronisieren und daraus die RTC stellen.

Nach erfolgreichem Zeit-Sync sollen die DS3231-Alarmregister neu geschrieben werden.

## 10. Home Assistant / MQTT

### 10.1 Bibliothek
Verwendet wird:
- Arduino Home Assistant Integration von Dawid Chyrzynski
- Referenz: `https://github.com/dawidchyrzynski/arduino-home-assistant`

### 10.2 Gerät
Das Arduino erscheint als ein HA-Gerät.

### 10.3 HA-Entitäten

#### Sensoren
- Temperatur
- Luftfeuchtigkeit
- Bodenfeuchte %
- Bodenfeuchte Rohwert
- Lüfter RPM

#### Switches
- Fan switch
- Fan auto mode
- Light auto mode
- Light hard power off
- Fallback-Verhalten bei Verbindungsverlust:
  - Licht aus
  - oder internen Auto-Mode nutzen

#### Light
- eine `light`-Entity für das Grow-Light
  - Brightness
  - ON/OFF
  - nur voll nutzbar bei `light_auto_mode = OFF`

#### Number-Entities
- Temperatur-Thresholds
- Feuchte-Thresholds
- Arduino-Lichtschedule-Zeiten
- Arduino-Standard-Dimmdauer
- Soil calibration values
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`

#### Buttons
- `sync time`
- `read soil raw value`
- `start_ha_dim`

### 10.4 HA-Dimmauftrag
Für den HA-Schedule-Dimmjob wird die Bedienung fest so modelliert:

- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

Verbindliches Verhalten:

- Diese drei Entities dienen ausschließlich dem **HA-Schedule-Betrieb**
- Sie sind nur wirksam, wenn `light_auto_mode = OFF`
- Beim Druck auf `button.start_ha_dim` startet der Arduino einen Dimmauftrag:
  - vom **aktuellen Ist-Zustand**
  - auf `ha_dim_target_percent`
  - innerhalb von `ha_dim_duration_minutes`
- Wenn `light_auto_mode = ON`, wird dieser Dimmauftrag ignoriert

Persistenzregel:

- Die Number-Entities bleiben Laufzeit-/Befehlsparameter und sind keine dauerhafte Konfiguration.
- Ist beim Neustart ein HA-Dimmjob relevant, wird dessen Wiederaufnahme über den `Light Resume State` rekonstruiert (Start/Ziel/Dauer/Startzeit auf RTC-/Epoch-Basis), nicht über `millis()`.

### 10.5 Zustandswiederherstellung
Separate Sensoren für Licht-Ist-Helligkeit oder Lichtmodus sind nicht erforderlich, solange:

- der Zustand der `light`-Entity korrekt publiziert wird
- die `switch`-Zustände korrekt publiziert werden
- nach Start oder MQTT-Reconnect die aktuellen Zustände aktiv erneut an HA gemeldet werden

## 11. Netzwerk / Verbindungsverhalten

### 11.1 Verbindung
Das Gerät verbindet sich mit:
- WLAN
- MQTT-Broker

### 11.2 Credentials
Alle Zugangsdaten in `Credentials.h`, mindestens:
- WiFi SSID
- WiFi Passwort
- MQTT Host
- MQTT Port
- MQTT User
- MQTT Passwort
- Device Name / ID / Prefix
- NTP-Server
- Zeitzone / Offset falls nötig

### 11.3 Availability / LWT
Das Gerät meldet Verfügbarkeit per MQTT und nutzt LWT.

### 11.4 Reconnect
Bei WLAN-/MQTT-Ausfall wird automatisch reconnectet.

### 11.5 Fallback nach Verbindungsverlust
Wenn die Verbindung länger als 10 Minuten nicht wiederhergestellt wird, soll ein konfigurierbares Verhalten für das Licht gelten:

- Licht ausschalten
- oder auf internen Arduino-Auto-Mode wechseln

Dieses Verhalten soll per HA einstellbar sein.

Andere lokale Funktionen wie Sensorik, RTC, EEPROM und Lüfterlogik laufen weiter.

## 12. Persistenz

### 12.1 Speicherort
Verwendet wird:

- `AT24C32` auf dem RTC-Modul
- Zugriff über **JC_EEPROM**

### 12.2 Bereits vorhandene persistente Konfiguration (weiterverwenden)
Bereits vorhanden und **nicht** doppelt neu anzulegen:

- `lightAutoMode`
- `fanAutoMode`
- `lightOnTimeMinutes`
- `lightOffTimeMinutes`
- `defaultLightDimMinutes`
- `lightFallbackMode`
- weitere Sensor-/Soil-/Threshold-Werte

### 12.3 Light Resume State (neu ergänzen)
Zusätzlich wird ein kleiner Resume-State für die Sollzustandsrekonstruktion benötigt, mindestens mit:

- letzter effektiver Helligkeit
- `hardPowerOffActive`
- Kennzeichen, ob ein HA-Dimmjob aktiv war
- Starthelligkeit
- Zielhelligkeit
- Job-Dauer
- Job-Startzeit auf RTC-/Epoch-Basis

### 12.4 Zeitbasis für Wiederaufnahme

- `millis()` reicht für Neustart-Wiederaufnahme nicht aus.
- Für eine korrekte Rekonstruktion nach Neustart ist eine RTC-/Epoch-basierte Zeitreferenz erforderlich.

### 12.5 Schreibverhalten

- Nur schreiben, wenn Werte sich wirklich geändert haben.
- Resume-State-Schreibvorgänge so auslegen, dass unnötige EEPROM-Last vermieden wird.

## 13. Architektur
### 13.1 Nicht-blockierend
- keine langen `delay()`
- Steuerung über `millis()`
- kleine I²C-Wartezeiten zulässig

### 13.2 Modulgrenzen
- SHTa: Sensor + Alert-Auswertung
- FanController: Lüfter schalten + RPM messen
- LightController: AD5263/Relais + Dimmaufträge
- ClockService: DS3231 + Alarmverwaltung + NTP-Sync
- HAInterface: HA-Entities + Commands
- NetworkManager: WiFi/MQTT + Reconnect
- Persistence: externes EEPROM

### 13.3 Fault-States (verbindlich)
Folgende Fault-States sind in der Firmware-Dokumentation verbindlich:

- `light_fault`
- `fan_fault`
- `sht_fault`
- `rtc_fault`
- `eeprom_fault`

Nicht verwenden:

- `sensor_fault`
- `system_fault`
- `last_fault_code`

Zusätzlich als Textzustand:

- `light_fault_reason`

Vorgesehene Inhalte von `light_fault_reason`:

- `ad5263_not_found`
- `ad5263_write_failed`
- `ad5263_readback_mismatch`

Verbindliche Verhaltensregeln:

- AD5263 beim Boot nicht erreichbar: Relais offen, Licht aus, `light_fault` und `light_fault_reason` setzen.
- AD5263-Fehler im Betrieb: Relais öffnen, `light_fault` setzen, `light_fault_reason` aktualisieren.
- Lüfterfehler: Wenn der Lüfter effektiv eingeschaltet sein soll, aber nach Karenzzeit keine Tachopulse anliegen, `fan_fault` setzen.
- Für Relais und Bodenfeuchtesensor keine starke Hardware-Selbstdiagnose behaupten, da kein echter Rückkanal vorhanden ist.

## 14. Übernommener vorhandener Code
### 14.1 SHTa
Übernommen werden soll insbesondere:

- `begin()`
- `startPeriodicMeasurement()`
- `blockingReadMeasurement()`
- `decodeStatusRegister()`
- `clearStatus()`
- `writeHighLimit()`
- `writeHighClear()`
- `writeLowLimit()`
- `writeLowClear()`
- `readLimit()`
- `limitPrinter()`
- `crc8()`
- `encodeAlertLimit()`

### 14.2 Alert-Struktur
- `alertTriggers[]` bleibt als Statuscontainer erhalten




