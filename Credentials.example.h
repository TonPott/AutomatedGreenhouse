#pragma once

// Beispiel-Datei.
// Kopiere diese Datei nach Credentials.h und trage dort deine echten Werte ein.

#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define MQTT_HOST       "MQTT.local"
#define MQTT_PORT       1883
#define MQTT_USERNAME   "mqtt_user"
#define MQTT_PASSWORD   "mqtt_password"

// Anzeigename / Geräteinformationen
#define DEVICE_NAME     "Grow Controller"
#define DEVICE_ID       "grow_controller_nano33iot"
#define MQTT_PREFIX     "homeassistant"

// Zeit / NTP
#define NTP_SERVER      "pool.ntp.org"

// Einfache Zeitzonenparameter.
// Optional kann die Umsetzung später auf eine genauere TZ-Logik erweitert werden.
#define UTC_OFFSET_SECONDS   3600
#define DST_OFFSET_SECONDS   3600
