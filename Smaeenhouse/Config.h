#pragma once

#include <Arduino.h>

// Pin mapping (Arduino Nano 33 IoT)
// Tach input is conditioned to 3.3V logic via documented 2N3904 stage.
constexpr uint8_t PIN_SHT_ALERT = 7;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_FAN_TACH = A1;
constexpr uint8_t PIN_LIGHT_DIM_SHDN = 4;
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

// AD5263 dimmer hardware
constexpr uint8_t AD5263_I2C_ADDRESS = 0x2C;
constexpr bool AD5263_AD0_TO_GND = true;
constexpr bool AD5263_AD1_TO_GND = true;
constexpr uint8_t AD5263_CHANNEL_W1 = 0;
constexpr uint8_t AD5263_CHANNEL_W2 = 1;

constexpr uint8_t LIGHT_DIM_MAPPING_SPLIT_PERCENT = 50;

// Hardware-specific effective limits are still subject to bench validation.
constexpr uint8_t LIGHT_DIM_W2_RDAC_MIN_EFFECTIVE = 0;
constexpr uint8_t LIGHT_DIM_W2_RDAC_MAX_EFFECTIVE = 255;
constexpr uint8_t LIGHT_DIM_W1_RDAC_MIN_EFFECTIVE = 0;
constexpr uint8_t LIGHT_DIM_W1_RDAC_MAX_EFFECTIVE = 255;

constexpr uint8_t LIGHT_DIM_W2_AT_0_PERCENT = 255;
constexpr uint8_t LIGHT_DIM_W2_AT_50_PERCENT = 0;
constexpr uint8_t LIGHT_DIM_W1_AT_50_PERCENT = 0;
constexpr uint8_t LIGHT_DIM_W1_AT_100_PERCENT = 255;

constexpr uint8_t LIGHT_DIM_COMMAND_MAX_ATTEMPTS = 2;
constexpr uint16_t LIGHT_DIM_WRITE_SETTLE_MS = 2;
constexpr uint16_t LIGHT_DIM_BOOT_SETTLE_MS = 2;
constexpr uint32_t LIGHT_DIM_STEP_INTERVAL_MS = 100UL;
constexpr uint32_t FAN_FAULT_GRACE_MS = FAN_RPM_PUBLISH_INTERVAL_MS;
constexpr uint32_t LIGHT_RESUME_INVALID_EPOCH = 0UL;

// WINGONEER Tiny DS3231 AT24C32 module
constexpr uint8_t RTC_EEPROM_I2C_ADDRESS = 0x57;
constexpr uint16_t RTC_EEPROM_PAGE_SIZE = 32;
constexpr uint16_t RTC_EEPROM_SIZE_BYTES = 4096;
constexpr uint32_t CONFIG_EEPROM_BASE_ADDRESS = 0;

// Persistent configuration defaults
constexpr uint16_t CONFIG_SCHEMA_VERSION = 3;

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

constexpr int16_t SOIL_CAL_MIN = 0;
constexpr int16_t SOIL_CAL_MAX = 1000;
constexpr int16_t SOIL_DEPTH_MIN_MM = 0;
constexpr int16_t SOIL_DEPTH_MAX_MM = 120;

constexpr int16_t DEFAULT_SOIL_AIR = 1000;
constexpr int16_t DEFAULT_SOIL_WATER = 500;
constexpr int16_t DEFAULT_SOIL_DEPTH_MM = 50;

