# HA_DASHBOARD.md

## Zweck

Dieses Dokument beschreibt das Home-Assistant-Frontend für den Grow Controller:
- Dashboard-Struktur
- Karten und Sichtbarkeit
- Bedienlogik je Modus
- Anzeige von Zeitwerten
- Kalibrierungsoberfläche für den Bodenfeuchtesensor

Dieses Dokument ergänzt:
- `SPEC.md`
- `MODULES.md`
- `entity-model.md`

Die Firmware bleibt die Quelle für Geräteverhalten und MQTT-Entities.
Dieses Dokument beschreibt nur die **Darstellung, Bedienung und UX in Home Assistant**.

---

## 1. Grundprinzip

Das Dashboard soll so aufgebaut sein, dass:

- häufige Funktionen schnell erreichbar sind
- nur bedienbare Funktionen sichtbar oder aktiv sind
- Auto-/Manuellogik für Licht und Lüfter klar erkennbar ist
- Kalibrierung geführt und fehlbedienungssicher ist
- Service- und Debug-Funktionen getrennt von der Alltagsbedienung bleiben

Das Dashboard soll ohne Kenntnisse der internen MQTT-Struktur verständlich sein.

---

## 2. Zielgruppen

### 2.1 Alltagsbedienung
Nutzer sollen:
- Zustände sehen
- Licht und Lüfter bedienen
- Auto-Modi ein-/ausschalten
- aktuelle Messwerte prüfen

### 2.2 Einrichtungs-/Servicebetrieb
Nutzer sollen:
- Kalibrierungswerte prüfen und ändern
- RTC-Sync manuell anstoßen
- HA-Dimmauftrag testen
- Problemzustände diagnostizieren

---

## 3. Dashboard-Struktur

Empfohlene Gliederung in **eine Hauptansicht** mit klar getrennten Bereichen:

1. **Übersicht**
2. **Licht**
3. **Klima & Lüfter**
4. **Bodenfeuchte & Kalibrierung**
5. **Service / Debug**

Wenn gewünscht, kann das später auf mehrere Views aufgeteilt werden. Für den Anfang ist eine gut strukturierte Hauptansicht ausreichend.

---

## 4. Bereich „Übersicht“

### Inhalt
- Temperatur
- Luftfeuchtigkeit
- Bodenfeuchte %
- Lüfterstatus / RPM
- Lichtstatus
- Auto-Mode für Licht
- Auto-Mode für Lüfter

### Ziel
Schneller Überblick ohne tiefe Bedienung.

### Empfohlene Darstellung
- kompakte Tile-/Entities-Karten
- klare Benennung
- Status zuerst, Einstellungen später

### Sichtbarkeit
Immer sichtbar.

---

## 5. Bereich „Licht“

## 5.1 Ziel
Dieser Bereich bündelt alles, was zur Lichtsteuerung gehört:
- aktueller Lichtzustand
- Auto/Manuell
- interner Arduino-Zeitplan
- HA-Dimmauftrag
- Hard Power Off

## 5.2 Immer sichtbare Elemente
- `light.grow_light`
- `switch.light_auto_mode`
- optional Statushinweis / Markdown-Karte:
  - „Arduino-Zeitplan aktiv“
  - oder „HA-Steuerung aktiv“

## 5.3 Nur sichtbar bei `light_auto_mode = ON`
Diese Elemente sollen nur sichtbar sein, wenn der Arduino-Zeitplan aktiv ist:

- `number.light_on_time_minutes`
- `number.light_off_time_minutes`
- `number.light_dim_minutes`

### Darstellung
- Es soll eine **lesbare Zeitdarstellung** gezeigt werden:
  - `light_on_time_minutes` → z. B. `08:30`
  - `light_off_time_minutes` → z. B. `20:15`

### Ziel
Nutzer sollen die Werte lesen können, ohne Minuten seit Mitternacht im Kopf umzurechnen.

## 5.4 Nur sichtbar bei `light_auto_mode = OFF`
Diese Elemente sollen nur sichtbar sein, wenn HA die Lichtsteuerung übernimmt:

- Helligkeitsslider der Light-Entity
- `switch.light_hard_power_off`
- `number.ha_dim_target_percent`
- `number.ha_dim_duration_minutes`
- `button.start_ha_dim`

### Ziel
Manuelle Lichtbedienung und HA-Dimmauftrag nur dann zeigen, wenn sie wirklich nutzbar sind.

## 5.5 Verhalten des Lichtbereichs
- Bei `light_auto_mode = ON` sollen HA-Dimmparameter und Hard-Power-Bedienung **nicht im Fokus stehen**
- Bei `light_auto_mode = OFF` sollen interne Arduino-Zeitplanparameter **ausgeblendet** oder deutlich inaktiv erscheinen

---

## 6. Bereich „Klima & Lüfter“

## 6.1 Inhalt
- Temperatur
- Luftfeuchtigkeit
- `switch.fan`
- `switch.fan_auto_mode`
- `sensor.fan_rpm`

## 6.2 Sichtbarkeit
Immer sichtbar.

## 6.3 Bedienlogik
### Wenn `fan_auto_mode = ON`
- manueller Lüfterschalter darf sichtbar bleiben
- Hinweis anzeigen:
  - „Automatik aktiv – Sensor-Alerts steuern den Lüfter“

### Wenn `fan_auto_mode = OFF`
- manueller Lüfterschalter normal nutzbar
- Hinweis:
  - „Manueller Betrieb“

---

## 7. Bereich „Bodenfeuchte & Kalibrierung“

## 7.1 Ziel
Normalbetrieb und Kalibrierung klar trennen.

## 7.2 Immer sichtbare Elemente
- `sensor.soil_moisture`

## 7.3 Kalibrierungsbereich
Zusätzlicher Block für die HA-geführte Kalibrierung.

### Sichtbar:
- nur wenn ein HA-Helfer „Kalibrierung aktiv“ gesetzt ist

### Inhalt
- Button / Aktion zum Start der Kalibrierung
- Anzeige des aktuellen Rohwerts
- geführte Schrittbeschreibung
- Buttons/Aktionen für:
  - Luftwert übernehmen
  - Wasserwert übernehmen
  - Tiefe setzen
  - Kalibrierung abschließen

### Wichtig
Die eigentliche Kalibrierungslogik lebt in HA-Skripten/Helpern.
Die Firmware liefert nur Rohwerte und speichert finale Kalibrierdaten.

---

## 8. Bereich „Service / Debug“

## Ziel
Weniger häufige oder technische Funktionen getrennt halten.

## Inhalt
- `button.sync_time`
- `button.read_soil_raw_value`
- optional Rohsensorwerte
- optionale Diagnosehinweise
- optional Fallback-Verhalten / Connectivity-Hinweis

## Sichtbarkeit
- am Ende des Dashboards
- optional in einklappbarer Karte / separater View

---

## 9. Sichtbarkeitsregeln

## 9.1 Licht
### Bei `switch.light_auto_mode = on`
anzeigen:
- Light-Status
- Arduino-Zeitplanwerte
- formatierte Zeitdarstellung

ausblenden:
- `ha_dim_target_percent`
- `ha_dim_duration_minutes`
- `button.start_ha_dim`

### Bei `switch.light_auto_mode = off`
anzeigen:
- Light-Entity mit Helligkeit
- HA-Dimmparameter
- Hard Power Off

ausblenden:
- Arduino-Zeitplanwerte oder zumindest als sekundär darstellen

## 9.2 Bodenfeuchte-Kalibrierung
Wenn ein HA-Helfer `input_boolean.soil_calibration_active` verwendet wird:
- Kalibrierungsblock nur bei aktivem Boolean sichtbar
- sonst nur Normalwerte anzeigen

## 9.3 Servicebereich
Technische Diagnosewerte optional nur anzeigen, wenn ein Service-/Debug-Modus aktiv ist.

---

## 10. Zeitdarstellung für `light_on_time_minutes` / `light_off_time_minutes`

Die Firmware und die MQTT-Number-Entities bleiben intern bei:
- Minuten seit Mitternacht

Im Dashboard soll eine lesbare Darstellung erscheinen, z. B.:
- `08:30`
- `20:15`

Empfehlung:
- Template-Entity oder Markdown-Karte aus Hilfswerten
- die Number-Entity bleibt für direkte Bearbeitung erhalten
- die menschenlesbare Zeit ist eine reine UI-Darstellung

---

## 11. UX-Regeln

- Nur bedienbare Controls prominent zeigen
- Technische Rohwerte sekundär darstellen
- Auto-/Manuellogik immer klar kennzeichnen
- Kalibrierung schrittweise erklären
- Alltagsfunktionen nicht mit Debug-Funktionen mischen

---

## 12. Mindestumfang für die erste Umsetzung

Für eine erste funktionale HA-Version sollen mindestens umgesetzt werden:

1. Hauptansicht mit den Bereichen:
   - Übersicht
   - Licht
   - Klima & Lüfter
   - Bodenfeuchte & Kalibrierung
   - Service

2. Bedingte Sichtbarkeit für:
   - Licht-Auto-Mode ON/OFF
   - optional Kalibrierung aktiv/inaktiv

3. Zeitdarstellung für:
   - `light_on_time_minutes`
   - `light_off_time_minutes`

4. Kalibrierungsbereich mit vorbereiteter UX-Struktur

5. HA-Dimmauftrag im Lichtbereich bei `light_auto_mode = OFF`

---

## 13. Nicht Teil dieses Dokuments

Nicht hier festgelegt werden:
- Firmware-Discovery-Details
- MQTT-Topicnamen
- RTC- oder EEPROM-Implementierung
- konkrete YAML- oder JSON-Syntax

Diese Dinge gehören in:
- Firmware
- `entity-model.md`
- `HA_AUTOMATIONS.md`
