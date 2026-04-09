## Technische Spezifikation der Module v4

## 1. Gesamtarchitektur

Das Projekt wird als Arduino-Sketchordner mit `.ino`, `.h` und `.cpp` aufgebaut.
Der Hauptsketch liegt unter `Smaeenhouse/Smaeenhouse.ino`; die zugehörigen Modul- und Konfigurationsdateien liegen im Ordner `Smaeenhouse/`.

Empfohlene Dateien:

- `Smaeenhouse/Smaeenhouse.ino`
- `Smaeenhouse/Config.h`
- `Smaeenhouse/Credentials.h` (lokal, nicht ins Repo committen)
- `Smaeenhouse/PersistentConfig.h/.cpp`
- `Smaeenhouse/RtcEepromStorage.h/.cpp`
- `Smaeenhouse/SHTa.h/.cpp`
- `Smaeenhouse/FanController.h/.cpp`
- `Smaeenhouse/LightController.h/.cpp`
- `Smaeenhouse/MoistureSensor.h/.cpp`
- `Smaeenhouse/ClockService.h/.cpp`
- `Smaeenhouse/NetworkManager.h/.cpp`
- `Smaeenhouse/HAInterface.h/.cpp`

### Grundprinzip
Jedes Modul soll möglichst diese Struktur haben:

- `begin()`
- `update(uint32_t nowMs)`

`loop()` ruft dann nur die `update()`-Methoden auf.

## 2. Config.h

### Aufgabe
Zentrale Compile-Time-Konstanten.

### Inhalt
- Pinbelegung
- Default-Werte
- Messintervalle
- Dimmer-Widerstandsgrenzen / Helligkeits-Mapping
- Standard-Thresholds
- Standard-Zeitplan
- Standard-Soil-Kalibrierung
- Hardware-Hinweise für Signalaufbereitung

### Typische Konstanten
- `PIN_SHT_ALERT`
- `PIN_RTC_ALARM`
- `PIN_FAN_SWITCH`
- `PIN_FAN_TACH`
- `PIN_LIGHT_DIM_SHDN`
- `PIN_LIGHT_POWER`
- `PIN_SOIL_SENSOR`

- `AD5263_I2C_ADDRESS = 0x2C`
- `AD5263_AD0_TO_GND = true`
- `AD5263_AD1_TO_GND = true`

- Kanalspezifische effektive RDAC-Grenzen für die verwendeten Dimmerkanäle (`W2` und `W1`), z. B.:
  - `LIGHT_DIM_W2_RDAC_MIN_EFFECTIVE`
  - `LIGHT_DIM_W2_RDAC_MAX_EFFECTIVE`
  - `LIGHT_DIM_W1_RDAC_MIN_EFFECTIVE`
  - `LIGHT_DIM_W1_RDAC_MAX_EFFECTIVE`
- `LIGHT_DIM_MAPPING_SPLIT_PERCENT = 50` (Split zwischen W2-Phase und W1-Phase)
- Hinweis: Die Bezeichner sind dokumentativ beispielhaft; verbindlich ist die getrennte Begrenzung pro Kanal.

- `DEFAULT_TEMP_HIGH_SET`
- `DEFAULT_TEMP_HIGH_CLEAR`
- `DEFAULT_HUM_HIGH_SET`
- `DEFAULT_HUM_HIGH_CLEAR`

- `DEFAULT_LIGHT_ON_TIME_MINUTES`
- `DEFAULT_LIGHT_OFF_TIME_MINUTES`
- `DEFAULT_LIGHT_DIM_MINUTES`

- `DEFAULT_SOIL_AIR`
- `DEFAULT_SOIL_WATER`
- `DEFAULT_SOIL_DEPTH_MM`

- `SOIL_PUBLISH_INTERVAL_MS = 10000`
- `TEMP_HUM_PUBLISH_INTERVAL_MS = 60000`
- `FAN_RPM_PUBLISH_INTERVAL_MS = 30000`

- `MQTT_FALLBACK_TIMEOUT_MS = 600000`

## 3. Credentials.h
### Aufgabe
Zentrale Ablage aller Zugangsdaten und installationsspezifischen IDs.

### Inhalt
- WLAN-SSID
- WLAN-Passwort
- MQTT-Host
- MQTT-Port
- MQTT-User
- MQTT-Passwort
- Device Name
- Device Unique ID
- MQTT Prefix
- NTP-Server
- Zeitzoneninformationen

## 4. PersistentConfig / externe EEPROM-Persistenz

### Aufgabe
Persistente Speicherung aller veränderbaren Laufzeitparameter im **AT24C32-EEPROM**
des WINGONEER Tiny DS3231 AT24C32 I2C Moduls über die Bibliothek **JC_EEPROM**.

### Datenstruktur
Eine zentrale Struct, z. B. `PersistentConfigData`, mit:

- Temp-/Hum-Thresholds
- Arduino-Lichtzeiten
- Arduino-Dimmdauer
- `fanAutoMode`
- `lightAutoMode`
- Fallback-Lichtverhalten
- Soil Air/Water/Depth
- Resume-State für Lichtwiederaufnahme (separate Teilstruktur oder klar abgegrenzter Block) mit:
  - letzter effektiver Helligkeit
  - `hardPowerOffActive`
  - Kennzeichen, ob ein HA-Dimmjob aktiv war
  - Job-Starthelligkeit
  - Job-Zielhelligkeit
  - Job-Dauer
  - Job-Startzeit auf RTC-/Epoch-Basis
- Versionsnummer
- CRC/Validierung

### API
- `bool begin(TwoWire& wire = Wire);`
- `bool configureEeprom();`
- `bool load(PersistentConfigData& out);`
- `bool save(const PersistentConfigData& cfg);`
- `bool saveIfChanged(const PersistentConfigData& cfg);`
- `void applyDefaults(PersistentConfigData& cfg);`

### Anforderungen
- keine unnötigen EEPROM-Schreibvorgänge
- Änderungen nur bei tatsächlicher Wertänderung speichern
- robuste Initialisierung bei leerem/ungültigem Speicher
- JC_EEPROM in eigene Projektlogik kapseln, nicht direkt quer durch das Projekt verwenden
- EEPROM-Zugriffe nie in ISR
- Die EEPROM-I²C-Adresse ist im Projekt bekannt und fest dokumentiert und lautet '0x57'.
- Die Implementierung soll diese feste Adresse direkt verwenden.
- Es soll keine automatische Adresssuche und kein I²C-Scan in der normalen Firmware-Initialisierung stattfinden.
- Die Adresse wird als Konstante zentral definiert, z. B. in `Config.h`.

### Technische Konsequenz:
- Der EEPROM-Wrapper bzw. die Persistenzklasse initialisiert `JC_EEPROM` direkt mit der bekannten Geräteadresse.
- Fehlerbehandlung bezieht sich auf Kommunikationsfehler oder ungültige Daten, nicht auf Adresssuche.
- Keine doppelte Neuanlage bereits vorhandener Konfigurationswerte; Persistenz gezielt um Resume-State ergänzen.
- Für Neustart-Wiederaufnahme reicht `millis()` nicht; Resume-State benötigt RTC-/Epoch-Zeitbasis.

## 5. SHTa

### Aufgabe
Kapselung des SHT3x inklusive Threshold-Programmierung und Alert-Auswertung.

### Übernommene Funktionen
- `begin()`
- `startPeriodicMeasurement(...)`
- `blockingReadMeasurement(...)`
- `decodeStatusRegister()`
- `clearStatus()`
- `writeHighLimit(...)`
- `writeHighClear(...)`
- `writeLowLimit(...)`
- `writeLowClear(...)`
- `readLimit(...)`
- `limitPrinter()`

### Zusätzliche fachliche Verantwortung
- Schreiben der konfigurierten Thresholds in den Sensor
- Auswertung von `alertTriggers[]`
- Erkennen von Sensor-Reset über `alertTriggers[4]`
- Re-Apply der Thresholds nach Reset

### API-Erweiterungen
Empfohlen:

- `void applyThresholdConfig(const PersistentConfigData& cfg);`
- `bool readMeasurement(float& temp, float& hum);`
- `bool hasTempAlert() const;`
- `bool hasHumAlert() const;`
- `bool hasSensorReset() const;`
- `void clearDecodedFlags();`

### ISR-Regel
Die eigentliche I²C-Kommunikation soll **nicht in der ISR** stattfinden.

Stattdessen:
- ISR setzt nur ein `volatile bool shtAlertPending`
- die Hauptlogik ruft danach `decodeStatusRegister()` auf

## 6. FanController

### Aufgabe
Schalten des Lüfters und Messen der RPM.

### Verantwortlichkeiten
- Fan-Schaltpin setzen
- RPM per Tachopulse zählen
- Auto-/Manuell-Logik umsetzen
- keine eigenen Threshold-Berechnungen

### Zustände
- `fanAutoMode`
- `fanManualState`
- `fanEffectiveState`
- `rpm`
- `fanFault`
- `tachPulseCount`

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `void setAutoMode(bool enabled);`
- `void setManualState(bool on);`
- `void setAutoDemand(bool on);`
- `bool isOn() const;`
- `uint16_t getRPM() const;`

### Steuerlogik
Effektiver Fan-Zustand:

- wenn `fanAutoMode == true`: Zustand folgt `autoDemand`
- wenn `fanAutoMode == false`: Zustand folgt `manualState`

`autoDemand` wird **außerhalb** des Moduls aus der SHT-Alert-Logik gesetzt.

### RPM-Logik
- ISR für Tach zählt Pulse
- alle 30 s RPM berechnen
- nur wenn Lüfter effektiv laufen soll
- beim Stillstand ggf. RPM auf 0 setzen
- wenn Lüfter effektiv an sein soll, aber nach Karenzzeit keine Tachopulse eintreffen: `fanFault` setzen

### Hardware-Hinweis Lüfter-Tacho
Die textliche Doku und der Schaltplan sollen die verwendete 2N3904-Stufe dokumentieren:

- Lüfter-Tacho über 10 kΩ auf 9 V pull-up
- Tacho über 47 kΩ an Basis eines 2N3904
- 100 kΩ von Basis nach GND
- Emitter an GND
- Collector an `PIN_FAN_TACH`
- 10 kΩ Pull-up vom Collector nach 3,3 V

Das Signal ist invertiert; der Code wählt entsprechend die Interrupt-Flanke.

## 7. LightController

### Aufgabe
Steuerung von AD5263-Dimmer, SHDN-Pin, Relais und Dimmaufträgen.

### Zentrale Verantwortung
- klare Trennung zwischen Arduino-Auto und HA-Steuerung
- Verwaltung des aktuellen Helligkeitszustands (`0..100 %`)
- Ausführung von Dimmjobs
- AD5263-Ansteuerung per I²C außerhalb von ISRs
- zwei AD5263-Kanäle gemäß festem Signalpfad nutzen
- SHDN-Steuerung mit sicherem Boot-Verhalten
- hartes Relais-Aus separat

### Verbindliche Hardwareannahmen
- AD5263BRUZ50 im I²C-Modus (`DIS = 1`)
- I²C-Adresse fest `0x2C` (`AD0 = GND`, `AD1 = GND`)
- Pull-ups auf SDA/SCL nach `3,3 V`
- SHDN liegt auf `PIN_LIGHT_DIM_SHDN` mit externem `10 kΩ` Pull-down
- kein interner Pull-up auf SHDN
- analoge Kanalverschaltung: `Dim+ -> W2 -> B2 -> A1 -> W1 -> Dim-`

### Helligkeitsmapping (verbindlich)

Referenzpunkte:

- `0 %  => W2=255, W1=0`
- `50 % => W2=0,   W1=0`
- `100 % => W2=0,  W1=255`

Zwischenbereiche:

- `0..50 %`: zuerst Kanal 2 herunterregeln (W2)
- `50..100 %`: danach Kanal 1 hochregeln (W1)

Dimmergrenzen:

- effektive untere/obere Grenzen werden über Firmware-Konstanten begrenzt
- diese Grenzen sind nicht über HA konfigurierbar

### Boot- und Ausschaltsequenz (verbindlich)

Beim Start:

1. AD5263 in `SHDN` halten
2. Konfiguration laden
3. Sollzustand aus persistierten Daten + Resume-State rekonstruieren
4. RDAC-Widerstandswerte setzen
5. `SHDN` freigeben
6. erst danach Relais schließen

Beim Ausschalten:

1. Relais öffnen
2. AD5263 in `SHDN`

Hinweis:

- Der AD5263 startet auf Midscale; ohne diese Reihenfolge droht ein falscher Helligkeitsstart.

### Wichtige interne Zustände
- `lightAutoMode`
- `currentBrightnessPercent`
- `targetBrightnessPercent`
- `powerCutActive`
- `controlSource`:
  - `ARDUINO_AUTO`
  - `HA_CONTROL`
- aktueller Dimmjob:
  - aktiv/inaktiv
  - Starthelligkeit
  - Zielhelligkeit
  - Startzeit
  - Dauer
  - Quelle
- `lightFault`
- `lightFaultReason`

### Resume-State (persistiert)
Mindestens:

- letzte effektive Helligkeit
- `hardPowerOffActive`
- Flag, ob HA-Dimmjob aktiv war
- Job-Starthelligkeit
- Job-Zielhelligkeit
- Job-Dauer
- Job-Startzeit (RTC-/Epoch-basiert)

Wichtig:

- Wiederaufnahme nach Neustart darf nicht nur auf `millis()` basieren.

### API
- `void begin();`
- `void update(uint32_t nowMs);`

- `void setAutoMode(bool enabled);`
- `bool isAutoMode() const;`

- `void applyManualBrightness(uint8_t percent);`
- `void applyManualOnOff(bool on);`
- `void applyHardPowerOff(bool off);`

- `void startArduinoDimJob(uint8_t targetPercent, uint32_t durationMs);`
- `void startHADimJob(uint8_t targetPercent, uint32_t durationMs);`

- `uint8_t getCurrentBrightness() const;`
- `bool isPowerOn() const;`
- `bool hasLightFault() const;`
- `const char* getLightFaultReason() const;`

### Detailregeln

#### Bei Auto-Mode ON
- `startHADimJob()` ignorieren
- `applyManualOnOff()` ignorieren oder no-op
- `applyManualBrightness()` darf optional den aktuellen Zielwert innerhalb des Zeitfensters überschreiben

#### Bei Auto-Mode OFF
- Arduino-Schedule-Events ignorieren
- HA-Schedule-Dimmjobs und manuelle Bedienung annehmen

#### Dimmjob-Verhalten
- ein neuer Job ersetzt den alten Job derselben aktiven Steuerwelt
- Helligkeit wird linear über die Laufzeit interpoliert
- bei `durationMs == 0` sofort setzen
- bei Ziel `0 %` darf optional Power-Off gesetzt werden, außer es handelt sich explizit um den separaten Hard-Power-Off-Schalter

#### Hard Power Off
- schaltet nur das Relais
- verändert gespeicherten Brightness-Wert nicht

#### Feste HA-Dimmjob-Schnittstelle
Der HA-Dimmjob wird verbindlich über folgende Entities ausgelöst:

- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

### Fehlerstrategie LightController

Verbindliche Readback-/Plausibilitätsstrategie (gestuft und rate-limited):

1. Bootphase vor `SHDN`-Freigabe und vor Relais-EIN:
   - AD5263 ansprechen
   - definierte W1/W2-RDAC-Werte schreiben
   - einmal Readback prüfen
   - nur bei konsistentem Ergebnis fortfahren

2. Bei diskreten Kommandos:
   - manueller Helligkeitssprung
   - Start Arduino-Dimmjob
   - Start HA-Dimmjob
   - Restore aus Resume-State
   jeweils:
   - Write
   - optional 1 kurzer Retry
   - Readback prüfen

3. Bei langen Dimmrampen:
   - kein Readback auf jedem Interpolationsschritt
   - Readback-Verifikation am Rampenende

Fehlerreaktion (verbindlich):

- AD5263 beim Boot nicht erreichbar:
  - Relais bleibt offen
  - Licht bleibt aus
  - `lightFault = true`
  - `lightFaultReason = "ad5263_not_found"`

- AD5263-Schreibfehler nach Retry:
  - Relais öffnen
  - `lightFault = true`
  - `lightFaultReason = "ad5263_write_failed"`

- AD5263-Readback-Mismatch nach Retry:
  - Relais öffnen
  - `lightFault = true`
  - `lightFaultReason = "ad5263_readback_mismatch"`

## 8. MoistureSensor

### Aufgabe
Lesen und Umrechnen des Bodenfeuchtesensors.

### Verantwortlichkeiten
- Rohwert lesen
- Prozentwert aus Air/Water kalibriert berechnen
- periodische 10-s-Messung

### Zustände
- `soilAir`
- `soilWater`
- `soilDepth`
- `lastRaw`
- `lastPercent`
- `lastReadMs`

### API
- `void begin(int air, int water, int depthMm);`
- `void update(uint32_t nowMs);`
- `int readRawNow();`
- `int getLastRaw() const;`
- `uint8_t getLastPercent() const;`
- `void setCalibration(int air, int water, int depthMm);`

### Regeln
- `update()` liest alle 10 s
- Prozent aus `map(raw, air, water, 0, 100)` mit Begrenzung 0..100
- keine Kenntnis über HA-Kalibrierroutine nötig

## 9. ClockService

### Aufgabe
Verwaltung von RTC_DS3231, deren Alarmen und NTP-Synchronisation.

### Verantwortlichkeiten
- RTC initialisieren
- NTP-Zeit holen
- RTC stellen
- aktuelle Uhrzeit liefern
- DS3231-SQW/INT für Alarmbetrieb konfigurieren
- tägliche Alarmzeiten im RTC-Register spiegeln
- Alarm-Flags auswerten und quittieren
- täglichen Re-Sync auslösen

### Zustände
- `lastSuccessfulSyncMs`
- `rtcAvailable`
- `timeValid`
- `alarmsConfigured`

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `bool syncFromNTP();`
- `DateTime now() const;`
- `uint16_t minutesSinceMidnight() const;`
- `bool isTimeValid() const;`
- `bool configureScheduleAlarms(uint16_t onMinutes, uint16_t offMinutes);`
- `ClockAlarmEvent serviceAlarmFlags();`

### Empfohlener Alarm-Event-Typ
```cpp
enum class ClockAlarmEvent {
    None,
    LightOnAlarm,
    LightOffAlarm
};
```

### Implementationshinweise
- Verwende RTClib-Funktionen der DS3231:
  - `writeSqwPinMode(...)`
  - `setAlarm1(...)`
  - `setAlarm2(...)`
  - `alarmFired(...)`
  - `clearAlarm(...)`
- SQW/INT ist ein Open-Drain-Ausgang; Arduino-Seite mit `INPUT_PULLUP` betreiben.
- Falls sich die Leitung im realen Aufbau als störanfällig erweist, kann zusätzlich ein externer 10-kΩ-Pull-up auf 3,3 V ergänzt werden.

### Regeln
- Sync beim Boot
- danach mindestens alle 24 h
- manueller Sync-Befehl aus HA möglich
- nach Zeit-Sync oder Konfigurationsänderung Alarmregister neu programmieren
- I²C-Zugriffe nur außerhalb der ISR

## 10. NetworkManager

### Aufgabe
WiFi- und MQTT-Verbindung verwalten.

### Verantwortlichkeiten
- WLAN verbinden
- MQTT verbinden
- Reconnect-Strategie
- Offline-Dauer messen
- Fallback-Bedingung nach 10 min erkennen

### Zustände
- `wifiConnected`
- `mqttConnected`
- `lastConnectionOkMs`
- `fallbackActive`

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `bool isWifiConnected() const;`
- `bool isMqttConnected() const;`
- `bool isOnline() const;`
- `bool isFallbackActive() const;`

### Regeln
- periodische Reconnect-Versuche
- online = WLAN + MQTT ok
- wenn >10 min offline: `fallbackActive = true`
- bei erfolgreicher Verbindung wieder `fallbackActive = false`

## 11. HAInterface

### Aufgabe
Abbildung aller HA-Entities und Verarbeitung der HA-Befehle.

### Verantwortlichkeiten
- Discovery / Device-Setup
- Publish von Zuständen und Messwerten
- Entgegennehmen von Commands
- Zustands-Re-Publish nach MQTT-Reconnect

### Benötigte Referenzen
Das Modul sollte auf andere Module zugreifen können, z. B. per Referenz im Konstruktor:

- `FanController&`
- `LightController&`
- `MoistureSensor&`
- `ClockService&`
- `SHTa&`
- `PersistentConfigManager&`
- `NetworkManager&`

### HA-Entities
- `HADevice`
- `HAMqtt`
- `HASensorNumber` für Temperatur, Feuchte, Soil%, SoilRaw, RPM
- `HABinarySensor` für `light_fault`, `fan_fault`, `sht_fault`, `rtc_fault`, `eeprom_fault`
- `HASensor` (Text) für `light_fault_reason`
- `HASwitch` für Fan, FanAuto, LightAuto, HardPowerOff, Fallback-Verhalten
- `HALight` für Grow-Light
- `HANumber` für Thresholds, Zeiten, Dimmdauer, Soil-Kalibrierung
- `HANumber` für `ha_dim_target_percent`
- `HANumber` für `ha_dim_duration_minutes`
- `HAButton` für `sync_time`
- `HAButton` für `read_soil_raw_value`
- `HAButton` für `start_ha_dim`

### API
- `void begin();`
- `void update(uint32_t nowMs);`
- `void onMqttConnected();`

### Zusätzliche Hilfsfunktionen
- `void publishAllStates();`
- `void publishSensorValues();`
- `void publishConfigValues();`

### Zusätzliche interne Zustände für HA-Dimmjob
- `uint8_t pendingHaDimTargetPercent;`
- `uint16_t pendingHaDimDurationMinutes;`

### Regeln
- beim Reconnect alle relevanten States erneut publizieren
- Light- und Switch-Zustände genügen; kein separater Sensor für Light-Ist-Helligkeit oder Lightmodus nötig
- `start_ha_dim` validiert:
  - Ziel 0..100
  - Dauer 0..1440 Minuten
- danach Aufruf:
  - `lightController.startHADimJob(target, durationMs)`

### Persistenzhinweis
Die Werte für:
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`

sind **nicht persistent**, sondern nur Laufzeit-/Befehlsparameter.
- Ein laufender HA-Dimmjob kann über den persistierten Resume-State (Start/Ziel/Dauer/Startzeit) nach Neustart rekonstruierbar gemacht werden.

### Fehlerstatus auf Systemebene
Die Firmware-Dokumentation nutzt folgende Fault-States:

- `light_fault`
- `fan_fault`
- `sht_fault`
- `rtc_fault`
- `eeprom_fault`

Nicht verwenden:

- `sensor_fault`
- `system_fault`
- `last_fault_code`

Zusätzlicher Textstatus (als Text-`sensor`):

- `light_fault_reason`

## 12. Smaeenhouse.ino

### Aufgabe
Zentrale Orchestrierung.

### Verantwortlichkeiten
- globale Modulinstanzen
- Setup-Reihenfolge
- ISR-Flags
- Aufrufreihenfolge in `loop()`

### Setup-Reihenfolge
1. Serial
2. Wire
3. Persistente Konfiguration aus AT24C32 laden oder Defaults
4. Module konstruieren/initialisieren
5. SHT-Thresholds anwenden
6. RTC initialisieren und Alarme konfigurieren
7. Netzwerk/HA starten
8. erste NTP-Sync versuchen
9. Interrupts aktivieren

### Loop-Reihenfolge
1. `nowMs = millis()`
2. `network.update(nowMs)`
3. `clock.update(nowMs)`
4. `sht`-Alert-Flag prüfen und Status auswerten
5. daraus `fan.setAutoDemand(...)`
6. `rtcAlarmPending` prüfen und `clock.serviceAlarmFlags()` ausführen
7. daraus ggf. `light.startArduinoDimJob(...)`
8. `fan.update(nowMs)`
9. `light.update(nowMs)`
10. `moisture.update(nowMs)`
11. `ha.update(nowMs)`

## 13. ISR-Strategie

Es sollte drei ISR-nahe Flags geben:

- `volatile bool shtAlertPending`
- `volatile bool rtcAlarmPending`
- Tach-ISR im FanController nur zum Pulse-Zählen

Wichtig:
- keine I²C-Operation in ISR
- keine MQTT-/HA-Operation in ISR
- keine aufwendige Logik in ISR

## 14. Logische Kernflüsse

### SHT Alert → Fan
1. Pin-7-Interrupt setzt Flag
2. Hauptloop liest Sensorstatus
3. Wenn Reset-Bit gesetzt → Thresholds erneut schreiben
4. Wenn Temp- oder Hum-Alert aktiv → `fan.setAutoDemand(true)`
5. Wenn keine relevanten Alerts aktiv → `fan.setAutoDemand(false)`

### RTC-Alarm → Arduino-Lichtschedule
1. DS3231-SQW/INT auf `PIN_RTC_ALARM` setzt Interrupt-Flag
2. Hauptloop ruft `clock.serviceAlarmFlags()` auf
3. `alarmFired(1)` / `alarmFired(2)` bestimmen, welcher Alarm ausgelöst wurde
4. Alarmflag wird gelöscht
5. je nach Event wird entsprechender Arduino-Dimmauftrag gestartet

### HA-Lichtbetrieb
1. Wenn `lightAutoMode == OFF`, akzeptiert LightController HA-Befehle
2. HA-Schedule startet explizite Dimmjobs
3. Manuelle Slider-/OnOff-Befehle wirken direkt

### Fallback
1. NetworkManager erkennt >10 min offline
2. je nach Konfiguration:
   - Licht aus
   - oder internen Auto-Mode aktivieren
3. lokale Funktionen laufen weiter

