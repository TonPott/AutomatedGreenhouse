# Home Assistant Entity Model

Diese Datei beschreibt die geplanten Home-Assistant-Entities und ihre Bedeutung.

## Gerät

Ein einzelnes HA-Gerät:

- Name: `Grow Controller`
- Plattform: ArduinoHA / MQTT Discovery

## Sensoren

### Temperatur
- Entity-Typ: `sensor`
- Name: `temperature`
- Richtung: Arduino → HA
- Einheit: `°C`

### Luftfeuchtigkeit
- Entity-Typ: `sensor`
- Name: `humidity`
- Richtung: Arduino → HA
- Einheit: `%`

### Bodenfeuchte Prozent
- Entity-Typ: `sensor`
- Name: `soil_moisture`
- Richtung: Arduino → HA
- Einheit: `%`

### Bodenfeuchte Rohwert
- Entity-Typ: `sensor`
- Name: `soil_raw`
- Richtung: Arduino → HA
- Einheit: roh / analog

### Lüfter-RPM
- Entity-Typ: `sensor`
- Name: `fan_rpm`
- Richtung: Arduino → HA
- Einheit: `rpm`

## Fault-Entities

### Light Fault
- Entity-Typ: `binary_sensor`
- Name: `light_fault`
- Richtung: Arduino → HA
- Bedeutung:
  - Fehler im Lichtpfad, besonders AD5263-/I²C-/Readback-bezogen

### Fan Fault
- Entity-Typ: `binary_sensor`
- Name: `fan_fault`
- Richtung: Arduino → HA
- Bedeutung:
  - Lüfter soll effektiv laufen, aber nach Karenzzeit keine Tachopulse

### SHT Fault
- Entity-Typ: `binary_sensor`
- Name: `sht_fault`
- Richtung: Arduino → HA
- Bedeutung:
  - SHT3x-Kommunikation oder Sensorstatus problematisch

### RTC Fault
- Entity-Typ: `binary_sensor`
- Name: `rtc_fault`
- Richtung: Arduino → HA
- Bedeutung:
  - DS3231 nicht verfügbar oder Zeitbasisproblem

### EEPROM Fault
- Entity-Typ: `binary_sensor`
- Name: `eeprom_fault`
- Richtung: Arduino → HA
- Bedeutung:
  - AT24C32 nicht verfügbar oder Persistenzproblem

### Light Fault Reason
- Entity-Typ: `sensor` (Text)
- Name: `light_fault_reason`
- Richtung: Arduino → HA
- Bedeutung:
  - textuelle Ursache für `light_fault`
- Beispiele:
  - `ad5263_not_found`
  - `ad5263_write_failed`
  - `ad5263_readback_mismatch`

Readback-/Plausibilitätsstrategie für Fault-Erkennung (Firmware-seitig):

1. Beim Boot vor `SHDN`-Freigabe und vor Relais-EIN prüfen.
2. Bei diskreten Lichtkommandos (manuell, Jobstart, Resume-Restore) per Write + Readback prüfen.
3. Bei langen Rampen nicht pro Zwischenschritt readbacken, sondern am Rampenende verifizieren.
4. Bei I²C/Write/Readback-Fehlern 1 kurzer Retry, danach Fault setzen und Relais öffnen.

Nicht Teil des Modells:

- `sensor_fault`
- `system_fault`
- `last_fault_code`

## Switches

### Fan Switch
- Entity-Typ: `switch`
- Name: `fan`
- Richtung: HA ↔ Arduino
- Bedeutung:
  - manuelles Ein/Aus des Lüfters

### Fan Auto Mode
- Entity-Typ: `switch`
- Name: `fan_auto_mode`
- Richtung: HA ↔ Arduino
- Bedeutung:
  - ON: SHT-Alert steuert den Lüfter
  - OFF: manuelle Lüftersteuerung

### Light Auto Mode
- Entity-Typ: `switch`
- Name: `light_auto_mode`
- Richtung: HA ↔ Arduino
- Bedeutung:
  - ON: Arduino-Schedule über DS3231-Alarme aktiv
  - OFF: HA-Steuerung aktiv

### Light Hard Power Off
- Entity-Typ: `switch`
- Name: `light_hard_power_off`
- Richtung: HA ↔ Arduino
- Bedeutung:
  - schaltet das Relais sofort
  - Dimmerzustand bleibt intern erhalten

### Fallback-Verhalten
- Entity-Typ: `switch`
- Name: `light_fallback_to_auto`
- Richtung: HA ↔ Arduino
- Bedeutung:
  - ON: bei längerem Offline-Zustand internen Arduino-Auto-Mode fürs Licht aktivieren
  - OFF: Licht bei längerem Offline-Zustand ausschalten

## Light

### Grow Light
- Entity-Typ: `light`
- Name: `grow_light`
- Richtung: HA ↔ Arduino
- Funktionen:
  - ON/OFF
  - Brightness

### Wichtige Regel
Die Light-Entity ist nur voll wirksam, wenn:

- `light_auto_mode = OFF`

Wenn `light_auto_mode = ON`:
- Brightness-Änderungen dürfen optional als temporäre Korrektur behandelt werden
- HA-Schedule-Trigger werden ignoriert
- Ein/Aus darf ignoriert oder deaktiviert werden

## Number Entities

### Temperatur / Feuchte Thresholds
- `temp_high_set`
- `temp_high_clear`
- `temp_low_set`
- `temp_low_clear`
- `hum_high_set`
- `hum_high_clear`
- `hum_low_set`
- `hum_low_clear`

Richtung:
- HA ↔ Arduino

Persistenz:
- ja, externes AT24C32-EEPROM via JC_EEPROM

### Arduino-Lichtschedule
- `light_on_time_minutes`
- `light_off_time_minutes`
- `light_dim_minutes`

Richtung:
- HA ↔ Arduino

Persistenz:
- ja, externes AT24C32-EEPROM via JC_EEPROM

Bemerkung:
- Zeitformat = Minuten seit Mitternacht
- Änderungen müssen zusätzlich die DS3231-Alarmregister aktualisieren

### Bodenfeuchte-Kalibrierung
- `soil_air`
- `soil_water`
- `soil_depth_mm`

Richtung:
- HA ↔ Arduino

Persistenz:
- ja, externes AT24C32-EEPROM via JC_EEPROM

### HA-Dimmauftrag
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`

Richtung:
- HA → Arduino

Persistenz:
- nein

Bedeutung:
- Laufzeit-/Befehlsparameter für einen expliziten HA-Dimmjob
- werden erst wirksam, wenn `start_ha_dim` ausgelöst wird
- Wiederaufnahme eines laufenden HA-Dimmjobs nach Neustart erfolgt über den intern persistierten Resume-State (nicht über die Number-States selbst)

## Buttons

### Sync Time
- Entity-Typ: `button`
- Name: `sync_time`
- Richtung: HA → Arduino
- Wirkung:
  - NTP-Sync anstoßen
  - RTC aktualisieren
  - DS3231-Alarme neu schreiben

### Read Soil Raw Value
- Entity-Typ: `button`
- Name: `read_soil_raw_value`
- Richtung: HA → Arduino
- Wirkung:
  - aktuellen Rohwert sofort auslesen und publizieren
  - gedacht für die HA-geführte Kalibrierung

### Start HA Dim
- Entity-Typ: `button`
- Name: `start_ha_dim`
- Richtung: HA → Arduino
- Wirkung:
  - startet einen Dimmauftrag von der aktuellen Ist-Helligkeit
  - Ziel = `ha_dim_target_percent`
  - Dauer = `ha_dim_duration_minutes`

### Wichtige Regel
`start_ha_dim` ist nur wirksam, wenn:

- `light_auto_mode = OFF`

Bei `light_auto_mode = ON` wird der Befehl ignoriert.

## State-Recovery / Re-Publish

Nach Start oder MQTT-Reconnect sollen aktiv erneut publiziert werden:

- Switch-Zustände
- Light-Zustand
- Number-Konfigurationswerte
- Fault-States inkl. `light_fault_reason`
- aktuelle Sensorwerte

Darum sind separate Diagnose-Sensoren für:

- Light-Ist-Helligkeit
- Lightmodus

nicht zwingend nötig.

## Optional spätere Diagnose-Entities

Nur falls gewünscht:

- aktiver Licht-Steuermodus
- aktueller AD5263-Code oder abgeleiteter Dimmer-Widerstand
- Fallback aktiv
- letzte NTP-Sync-Zeit
- RTC-Alarmzustand

Diese sind optional und nicht Teil des Pflichtumfangs.
