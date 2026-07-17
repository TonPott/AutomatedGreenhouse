#pragma once

// Copy this file to Credentials.h and replace the placeholders locally.
// Credentials.h is ignored by Git and must never be committed.

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define MQTT_HOST "MQTT.local"
#define MQTT_PORT 1883
#define MQTT_USERNAME "mqtt_user"
#define MQTT_PASSWORD "mqtt_password"

// The OTA name should not contain spaces. The password is used by the
// JAndrassy ArduinoOTA upload tool and is independent of the WiFi password.
#define OTA_NAME "GrowControllerTests"
#define OTA_PASSWORD "replace_with_a_strong_ota_password"

// Optional: set this if the installed SHT31 address differs from the default 0x44.
// #define SHT31_I2C_ADDRESS 0x45
