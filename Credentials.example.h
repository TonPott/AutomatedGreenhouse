#pragma once

// Example file.
// Copy this file to Credentials.h and enter your real values there.

#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define MQTT_HOST       "MQTT.local"
#define MQTT_PORT       1883
#define MQTT_USERNAME   "mqtt_user"
#define MQTT_PASSWORD   "mqtt_password"

// Display name / device information
#define DEVICE_NAME     "Grow Controller"
#define DEVICE_ID       "grow_controller_nano33iot"
#define MQTT_PREFIX     "homeassistant"

// Time / NTP
#define NTP_SERVER      "pool.ntp.org"

// Simple timezone parameters.
// Optionally, this can later be extended to more precise TZ logic.
#define UTC_OFFSET_SECONDS   3600
#define DST_OFFSET_SECONDS   3600
