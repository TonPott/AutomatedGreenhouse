# HA_AUTOMATIONS.md

## Zweck

Dieses Dokument beschreibt die benötigten Home-Assistant-Helfer, Skripte, Templates und Automationen für das Grow-Controller-Frontend.

Es ergänzt `HA_DASHBOARD.md` und beschreibt:
- Hilfs-Entities
- Template-Darstellungen
- Kalibrierungsablauf
- Scheduler-Anbindung
- Bedienlogik in HA

Die Firmware bleibt für Geräteverhalten und Zustandslogik zuständig.
Home Assistant übernimmt hier nur:
- Darstellung
- Workflow
- UI-geführte Abläufe

---

## 1. Grundprinzip

Home Assistant soll:
- Firmware-Entities darstellen
- fehlende UX-Schichten ergänzen
- den Benutzer durch Kalibrierung und HA-Dimmauftrag führen
- bedingte Sichtbarkeit und Hilfstexte ermöglichen

Keine HA-Automation soll Firmwarelogik duplizieren, die bereits im Arduino definiert ist.

---

## 2. Empfohlene HA-Helfer

## 2.1 Für Bodenfeuchte-Kalibrierung
Empfohlene Helper:

- `input_boolean.soil_calibration_active`
- `input_select.soil_calibration_step`
- `input_number.soil_calibration_air_candidate`
- `input_number.soil_calibration_water_candidate`
- `input_number.soil_calibration_depth_candidate`
- optional `input_text.soil_calibration_hint`

### Zweck
Diese Helper halten den UI-Zustand der Kalibrierung in HA.
Die Firmware braucht davon nichts zu wissen.

## 2.2 Für formatierte Zeitdarstellung
Optional:
- keine zusätzlichen Helper nötig, wenn Templates direkt aus den Number-Entities erzeugt werden

## 2.3 Für Dashboard-Modi
Optional:
- `input_boolean.grow_controller_debug_mode`

Zweck:
- technische Diagnoseelemente nur bei Bedarf einblenden

---

## 3. Template-Entities

## 3.1 Formatierte Zeitdarstellung
Es sollen Template-Entitäten oder Template-Ausgaben erzeugt werden für:

- `light_on_time_minutes`
- `light_off_time_minutes`

### Ziel
Umwandlung von:
- Minuten seit Mitternacht

in:
- `HH:MM`

### Erwartetes Verhalten
Beispiele:
- `510` → `08:30`
- `1215` → `20:15`

### Hinweis
Die zugrunde liegenden MQTT-Number-Entities bleiben unverändert.
Die formatierte Zeit ist nur eine Darstellungs-/UX-Schicht.

---

## 4. Kalibrierungsablauf Bodenfeuchte

## 4.1 Ziel
Die Kalibrierung soll in HA geführt werden, ohne Firmware-seitigen Wizard.

## 4.2 Grundablauf
1. Benutzer startet Kalibrierung
2. HA setzt:
   - `soil_calibration_active = on`
   - `soil_calibration_step = air`
3. Benutzer hält Sensor in Luft
4. Benutzer liest/aktualisiert Rohwert
5. Benutzer übernimmt Luftwert
6. HA speichert Kandidatenwert
7. HA wechselt zu Schritt `water`
8. Benutzer taucht Sensor in Wasser
9. Benutzer liest/aktualisiert Rohwert
10. Benutzer übernimmt Wasserwert
11. HA wechselt zu Schritt `depth`
12. Benutzer setzt/prüft Einstecktiefe in mm
13. HA wechselt zu Schritt `confirm`
14. Finale Werte werden auf die Firmware-Entities geschrieben:
   - `number.soil_air`
   - `number.soil_water`
   - `number.soil_depth_mm`
15. Kalibrierungsmodus endet

## 4.3 Benötigte Benutzeraktionen
- Kalibrierung starten
- Rohwert lesen
- Luftwert übernehmen
- Wasserwert übernehmen
- Tiefe übernehmen
- Abschluss bestätigen
- optional Abbrechen

## 4.4 Firmware-seitige Rolle
Die Firmware muss nur:
- Rohwert liefern
- finale Werte als Number-Commands annehmen
- diese persistent speichern

---

## 5. Nutzung des Buttons `read_soil_raw_value`

## Zweck
Dieser Button dient als expliziter Trigger, um während der Kalibrierung den Rohwert neu zu lesen und zu publizieren.

## HA-Verwendung
- Der Button wird in Skripten/Aktionen genutzt
- Nach Auslösen sollte HA kurz warten und dann den Rohwert lesen/anzeigen

Empfehlung:
- kleine Verzögerung im Script einplanen, falls die Firmware nicht sofort publiziert

---

## 6. Scheduler-Anbindung für HA-Dimmauftrag

## 6.1 Ziel
Wenn `light_auto_mode = OFF`, soll HA zeitgesteuerte Dimmaufträge an den Arduino senden können.

## 6.2 Verwendete Firmware-Entities
- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

## 6.3 HA-Ablauf
Ein Scheduler-/Script-Ablauf setzt:
1. `number.ha_dim_target_percent`
2. `number.ha_dim_duration_minutes`
3. `button.start_ha_dim`

## 6.4 Wichtige Regel
Vor dem Auslösen soll HA prüfen:
- `switch.light_auto_mode == off`

Wenn `light_auto_mode = on`, soll der HA-Dimmauftrag nicht angeboten oder nicht ausgelöst werden.

---

## 7. Sichtbarkeitslogik im Dashboard

## 7.1 Lichtbereich
### Sichtbar bei `light_auto_mode = on`
- Arduino-Zeitplan
- formatierte Zeitwerte

### Sichtbar bei `light_auto_mode = off`
- Helligkeitsslider
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`
- `button.start_ha_dim`
- `switch.light_hard_power_off`

## 7.2 Kalibrierungsbereich
### Sichtbar wenn:
- `input_boolean.soil_calibration_active = on`

### Sonst:
- nur normale Soil-Werte anzeigen

## 7.3 Debug-Bereich
### Sichtbar wenn:
- `input_boolean.grow_controller_debug_mode = on`

---

## 8. Empfohlene Skripte

## 8.1 `script.soil_calibration_start`
Setzt:
- Kalibrierung aktiv
- Schritt = air
- Kandidaten zurücksetzen

## 8.2 `script.soil_capture_air`
- `button.read_soil_raw_value` auslösen
- kurzen Moment warten
- Rohwert in `soil_calibration_air_candidate` übernehmen
- Schritt = water

## 8.3 `script.soil_capture_water`
- `button.read_soil_raw_value` auslösen
- warten
- Rohwert in `soil_calibration_water_candidate` übernehmen
- Schritt = depth

## 8.4 `script.soil_apply_calibration`
- Kandidatenwerte auf die echten Number-Entities schreiben:
  - `soil_air`
  - `soil_water`
  - `soil_depth_mm`
- Kalibrierung beenden

## 8.5 `script.soil_calibration_cancel`
- Kalibrierungsstatus zurücksetzen
- nichts an Firmware schreiben

## 8.6 `script.start_ha_dim_job`
- prüft `light_auto_mode`
- setzt Zielwert
- setzt Dauer
- drückt `button.start_ha_dim`

---

## 9. Empfehlungen für die erste Implementierung

## 9.1 Muss in der ersten Version enthalten sein
- formatierte Zeitdarstellung für Licht-On/Off
- Kalibrierung aktiv/inaktiv
- einfacher Kalibrierungsablauf
- HA-Dimmauftrag als Script/Scheduler-Kette
- bedingte Sichtbarkeit im Dashboard

## 9.2 Kann später folgen
- Debug-View
- hübschere Texte / Markdown-Hinweise
- visuelle Schrittanzeige für Kalibrierung
- bessere Fehlermeldungen / Bestätigungen

---

## 10. Branching-Empfehlung

Für die HA-Konfiguration ist ein eigener Branch sinnvoll, wenn:
- Dashboard-Dateien
- Helpers
- Scripts
- Automationen
- optionale Lovelace-YAMLs

getrennt von Firmwarearbeit entwickelt werden sollen.

Empfehlung:
- eigener Branch für HA-Frontend / HA-Konfiguration
- Basis idealerweise auf dem Branch, der die aktuell gültigen Entities und Doku enthält

Begründung:
- getrennte Reviewbarkeit
- Firmware- und HA-Arbeit blockieren sich weniger
- Dashboard-/UX-Änderungen können unabhängig iteriert werden

---

## 11. Nicht Teil dieses Dokuments

Nicht hier definiert werden:
- konkrete MQTT-Discovery-Payloads
- Firmware-Button-/Entity-Implementierung
- RTC-/EEPROM-Logik
- elektrische Hardwaredetails
