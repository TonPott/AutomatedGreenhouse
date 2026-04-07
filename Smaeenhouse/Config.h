#pragma once

#include <Arduino.h>

// Pin mapping (Arduino Nano 33 IoT)
// Tach input is conditioned to 3.3V logic via documented 2N3904 stage.
// Final hardware spec uses AD5263 + relay. Pin 4 is reserved for AD5263 SHDN control (legacy constant name remains until firmware refactor).
constexpr uint8_t PIN_SHT_ALERT = 7;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_FAN_TACH = A1;
constexpr uint8_t PIN_LIGHT_PWM = 4;
constexpr uint8_t PIN_LIGHT_POWER = 3;
constexpr uint8_t PIN_SOIL_SENSOR = A0;

// Runtime timings
constexpr uint32_t SOIL_PUBLISH_INTERVAL_MS = 10000UL;
constexpr uint32_t TEMP_HUM_PUBLISH_INTERVAL_MS = 60000UL;
constexpr uint32_t FAN_RPM_PUBLISH_INTERVAL_MS = 30000UL;
constexpr uint32_t SHT_STATUS_POLL_INTERVAL_MS = 5000UL;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000UL;
constexpr uint32_t NTP_RETRY_INTERVAL_MS = 60000UL;
constexpr uint32_t MQTT_FALLBACK_TIMEOUT_MS = 600000UL;  // 10 minutes
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 86400000UL;  // 24 hours

// Legacy PWM dim constants (kept for transition until AD5263 firmware mapping is implemented)
constexpr uint8_t PWM_FULL_ON = 0;
constexpr uint8_t PWM_DARKEST_STABLE = 120;
constexpr uint8_t PWM_OFF_THRESHOLD = 160;
constexpr uint8_t PWM_SIGNAL_OFF = 255;

// WINGONEER Tiny DS3231 AT24C32 module
constexpr uint8_t RTC_EEPROM_I2C_ADDRESS = 0x57;
constexpr uint16_t RTC_EEPROM_PAGE_SIZE = 32;
constexpr uint16_t RTC_EEPROM_SIZE_BYTES = 4096;
constexpr uint32_t CONFIG_EEPROM_BASE_ADDRESS = 0;

// Persistent configuration defaults
constexpr uint16_t CONFIG_SCHEMA_VERSION = 2;

constexpr float DEFAULT_TEMP_HIGH_SET = 30.0f;
constexpr float DEFAULT_TEMP_HIGH_CLEAR = 28.0f;
constexpr float DEFAULT_TEMP_LOW_SET = 18.0f;
constexpr float DEFAULT_TEMP_LOW_CLEAR = 20.0f;

constexpr float DEFAULT_HUM_HIGH_SET = 85.0f;
constexpr float DEFAULT_HUM_HIGH_CLEAR = 80.0f;
constexpr float DEFAULT_HUM_LOW_SET = 35.0f;
constexpr float DEFAULT_HUM_LOW_CLEAR = 40.0f;

constexpr uint16_t DEFAULT_LIGHT_ON_TIME_MINUTES = 8 * 60;
constexpr uint16_t DEFAULT_LIGHT_OFF_TIME_MINUTES = 20 * 60;
constexpr uint16_t DEFAULT_LIGHT_DIM_MINUTES = 30;

constexpr bool DEFAULT_FAN_AUTO_MODE = true;
constexpr bool DEFAULT_LIGHT_AUTO_MODE = true;

constexpr int16_t DEFAULT_SOIL_AIR = 3000;
constexpr int16_t DEFAULT_SOIL_WATER = 1500;
constexpr int16_t DEFAULT_SOIL_DEPTH_MM = 50;

