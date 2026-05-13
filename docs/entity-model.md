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

### Bodenfeuchte %
- Entity-Typ: `sensor`
- Name: `soil_moisture_percent`
- Richtung: Arduino → HA
- Einheit: `%`
- Bedeutung:
  - berechneter Wert aus `soil_air`, `soil_water`, aktuellem Rohwert und `soil_depth_mm`
  - Luftreferenz entspricht 0 %
  - Wasserreferenz bei 120 mm entspricht 100 %
  - gültige Werte sind auf 0..100 % begrenzt
  - bei `soil_depth_mm < 20` soll der Wert unavailable/invalid sein

### Bodenfeuchte Rohwert
- Entity-Typ: `sensor`
- Name: `soil_moisture_raw`
- Richtung: Arduino → HA
- Einheit: roh / analog
- Bedeutung:
  - aktueller ADC-bezogener Rohwert des Sensors
  - darf auch publiziert werden, wenn die Prozentberechnung ungültig ist

### Lüfter-RPM
- Entity-Typ: `sensor`
- Name: `fan_rpm`
- Richtung: Arduino → HA
- Einheit: `rpm`

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
- Entity-Typ: `switch` oder alternatives Mapping
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

Grenzen:
- `soil_air`: min 0, max 1000, step 1
- `soil_water`: min 0, max 1000, step 1
- `soil_depth_mm`: nutzereingetragener Millimeterwert mit projektdefinierten Grenzen

Hinweise:
- `soil_air` und `soil_water` verwenden den erwarteten realen Projektbereich; Luftwerte werden unter ca. 900 erwartet.
- Die Firmware darf interne ADC-Sicherheitsgrenzen separat hart auf 0..4095 setzen.
- `soil_depth_mm` ist ein aktiver Korrekturparameter, nicht nur informativ.
- Werte unter 20 mm gelten für die Prozentberechnung als ungültig, weil die erste physische Sensormarkierung bei 20 mm liegt.

Tiefenkorrektur:

```text
SOIL_REFERENCE_DEPTH_MM = 120
depth_factor = soil_depth_mm / SOIL_REFERENCE_DEPTH_MM
percent = (soil_air - raw) / ((soil_air - soil_water) * depth_factor) * 100
```

Die lineare Tiefenkorrektur ist eine bewusste Näherung. Eine Messreihe existiert,
das Projekt akzeptiert dieses Modell derzeit aber als ausreichend genau.

### HA-Dimmauftrag
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`

Richtung:
- HA → Arduino

Persistenz:
- nein

Bedeutung:
- Parameter für einen expliziten HA-Dimmjob
- werden erst wirksam, wenn `start_ha_dim` ausgelöst wird

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

## Bodenfeuchte-Kalibrierungsworkflow

Die Firmware verwaltet keinen Kalibrierungsassistenten und keine interne
Kalibrierungs-State-Machine. Home Assistant orchestriert den Ablauf mit dem
generischen Button `read_soil_raw_value` und den drei persistenten Number
Entities.

Air step:
- Nutzer legt den Sensor trocken in Luft.
- HA ruft `button.read_soil_raw_value` auf.
- HA wartet kurz, bis `sensor.soil_moisture_raw` aktualisiert ist.
- HA schreibt diesen Wert in `number.soil_air`.

Water step:
- Nutzer legt den Sensor bei der Referenztiefe von 120 mm in Wasser.
- HA ruft `button.read_soil_raw_value` auf.
- HA wartet kurz, bis `sensor.soil_moisture_raw` aktualisiert ist.
- HA schreibt diesen Wert in `number.soil_water`.

Depth step:
- Nutzer trägt die reale Einstecktiefe im Substrat manuell in `number.soil_depth_mm` ein.
- Die Firmware nutzt diesen Wert aktiv für die Prozentberechnung.

Separate Firmware-Buttons wie `capture_soil_air` oder `capture_soil_water`
sollen nicht ergänzt werden. Der generische Raw-Read-Button plus HA-Scripts oder
HA-Automationen reicht aus und vermeidet unnötige MQTT-Entities.

## State-Recovery / Re-Publish

Nach Start oder MQTT-Reconnect sollen aktiv erneut publiziert werden:

- Switch-Zustände
- Light-Zustand
- Number-Konfigurationswerte
- aktuelle Sensorwerte

Darum sind separate Diagnose-Sensoren für:
- Light-Ist-Helligkeit
- Lightmodus

nicht zwingend nötig.

## Optional spätere Diagnose-Entities

Nur falls gewünscht:

- aktiver Licht-Steuermodus
- aktueller PWM-Rohwert
- Fallback aktiv
- letzte NTP-Sync-Zeit
- RTC-Alarmzustand

Diese sind optional und nicht Teil des Pflichtumfangs.
