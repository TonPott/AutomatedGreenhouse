Repository documentation may be in German. Implement code, identifiers, comments, and commit summaries in English only.

## Zweck

Diese Datei definiert verbindliche Arbeitsregeln für Codex und andere KI-Agenten in diesem Repository.

## Projektkontext

Dies ist ein Arduino-IDE-kompatibles Firmware-Projekt für einen **Arduino Nano 33 IoT**.
Die Firmware steuert Sensorik, Lüfter, Licht, RTC, externes EEPROM und Home-Assistant-MQTT-Integration.

## Verbindliche Arbeitsregeln

### 1. Projektstruktur beibehalten
- Das Projekt bleibt ein klassischer Arduino-Sketchordner.
- `Smaeenhouse.ino` bleibt im Repo-Wurzelverzeichnis.
- Header- und CPP-Dateien bleiben im gleichen Ordner wie `Smaeenhouse.ino`, sofern nicht ausdrücklich anders gewünscht.

### 2. Credentials niemals überschreiben
- `Credentials.h` ist lokal und geheim.
- Niemals echte Zugangsdaten in das Repo schreiben.
- Nur `Credentials.example.h` darf als Vorlage verändert werden.

### 3. Keine I²C-Logik in ISR
- ISR dürfen nur Flags setzen oder Pulse zählen.
- Keine Wire-/I²C-Kommunikation in ISR.
- Keine Sensorabfragen in ISR.
- Das gilt ausdrücklich auch für:
  - SHT-Alert
  - DS3231-SQW/INT
  - externe AT24C32-EEPROM-Zugriffe

### 4. Keine MQTT-/HA-Logik in ISR
- MQTT, ArduinoHA und Publishes nur im Hauptloop bzw. in normalen Methoden.
- ISR bleiben minimal.

### 5. Light-Logik strikt trennen
Es gibt zwei Steuerwelten:
- `light_auto_mode = ON` → Arduino-Schedule aktiv, HA-Schedule ignorieren
- `light_auto_mode = OFF` → HA-Steuerung aktiv, Arduino-Schedule ignorieren

Diese Trennung darf nicht aufgeweicht werden.

### 6. HA-Dimmjob nur über die festgelegten Entities
Zeitgesteuerte HA-Dimmjobs werden ausschließlich über folgende Entities modelliert:
- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

Keine alternativen JSON-/String-Kommandos einführen, außer der Nutzer fordert das explizit.

### 7. Persistenz über externes RTC-EEPROM
- Verwende das **AT24C32-EEPROM** des WINGONEER Tiny DS3231 AT24C32 I2C Moduls.
- Keine `SAMD_SafeFlashStorage`-Persistenz mehr verwenden.
- Verwende für das externe EEPROM die Bibliothek **JC_EEPROM**.
- Kapsle den EEPROM-Zugriff in einer projektinternen Persistenzschicht.
- Schreibe nur bei tatsächlichen Änderungen.
- Vermeide unnötig häufige Writes im normalen Laufbetrieb.

### 8. RTC-Alarme für den Arduino-internen Lichtplan verwenden
- Nutze die beiden DS3231-Alarme für den internen Arduino-Lichtplan.
- `Alarm1` und `Alarm2` repräsentieren die gespeicherten Ein-/Aus- bzw. Dim-On-/Dim-Off-Zeiten.
- Die Zeiten aus der persistierten Konfiguration werden nach Boot, Zeit-Sync und Konfigurationsänderungen in die DS3231-Alarmregister geschrieben.
- Der SQW/INT-Ausgang der DS3231 wird an `PIN_RTC_ALARM` angeschlossen.
- Die ISR setzt nur ein Flag; Auswertung erfolgt im Hauptloop über RTClib.

### 9. Bibliotheken nicht unnötig erweitern
Keine neuen Bibliotheken einführen, wenn:
- vorhandene Projektbibliotheken genügen
- die Funktion mit Standard-Arduino-Mitteln umsetzbar ist

Wenn doch eine neue Bibliothek nötig ist:
- Begründung in der Änderung dokumentieren
- `libraries.txt` aktualisieren

### 10. Home-Assistant-States sauber republishen
Nach Start oder MQTT-Reconnect müssen:
- Light-State
- Switch-States
- Number-Konfigurationswerte
- relevante Sensorwerte

erneut an HA publiziert werden.

### 11. FanController bleibt dumm bezüglich Thresholds
- FanController darf keine eigenen Temperatur-/RH-Thresholds auswerten.
- Die Entscheidung kommt ausschließlich aus der SHT-Alert-Logik.

### 12. Bodenfeuchte-Kalibrierung bleibt HA-gesteuert
- Keine interne Kalibrierroutine in der Firmware erfinden.
- Firmware liefert Rohwerte und speichert finale Kalibrierdaten.
- HA führt den Benutzer durch die Routine.

### 13. Änderungen zuerst an Doku anlehnen
Vor größeren Codeänderungen:
- `SPEC.md` beachten
- `MODULES.md` beachten
- `entity-model.md` beachten
- vorhandenen Schaltplan im Projektordner beachten

### 14. SHTa-Routinen verwenden
- `SHTa.h/.cpp` sollen weiterverwendet werden
- vor Änderungen mit Begründung nachfragen.

### 15. Dokumentation für neue Nutzer verständlich halten
- Hardware-Abschnitte so formulieren, dass das Projekt auch beim ersten Lesen verständlich bleibt.
- Schaltplan im Projektordner als Primärreferenz erwähnen.
- Textliche Doku soll zusätzlich die Signalaufbereitung für:
  - Licht-Optokoppler (RC-Glied)
  - Lüfter-Tacho (2N3904-Stufe)
  beschreiben.

Wenn Code und Doku widersprechen, gilt zuerst die Doku — außer der Nutzer sagt ausdrücklich etwas anderes.

## Bevorzugte Arbeitsweise

1. Compile-Fehler zuerst beheben
2. Danach Zustandslogik bereinigen
3. Danach HA-Entity-Mapping vervollständigen
4. Danach Feinschliff / Logging / Debugging

## Nicht erwünscht

- große Logikblöcke direkt in `Smaeenhouse.ino`
- versteckte globale Nebenwirkungen
- blockierende `delay()`-Ketten
- Bibliotheken vendoren/einchecken ohne Grund
- spontane Änderung der Repo-Struktur
