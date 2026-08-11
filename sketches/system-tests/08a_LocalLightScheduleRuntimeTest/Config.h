#pragma once

#include <Arduino.h>

// Arduino Nano 33 IoT pin mapping used by the installed cabinet hardware.
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_LIGHT_POWER = 3;
constexpr uint8_t PIN_LIGHT_DIM_SHDN = 4;
constexpr uint8_t PIN_LIGHT_SENSOR_INT = 9;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_SOIL_SENSOR = A0;

constexpr uint8_t FAN_OFF_LEVEL = LOW;
constexpr uint8_t LIGHT_RELAY_OPEN_LEVEL = LOW;
constexpr uint8_t LIGHT_DIM_SHDN_ASSERTED_LEVEL = LOW;

constexpr uint8_t I2C_ADDRESS_TSL2591 = 0x29;
constexpr uint8_t I2C_ADDRESS_AD5263 = 0x2C;
constexpr uint8_t I2C_ADDRESS_AT24C32 = 0x57;
constexpr uint8_t I2C_ADDRESS_DS3231 = 0x68;

constexpr uint8_t AD5263_CHANNEL_W1 = 0;
constexpr uint8_t AD5263_CHANNEL_W2 = 1;
constexpr uint8_t LIGHT_DIM_MAPPING_SPLIT_PERCENT = 50;
constexpr uint8_t LIGHT_DIM_W2_AT_0_PERCENT = 0;
constexpr uint8_t LIGHT_DIM_W1_AT_0_PERCENT = 255;
constexpr uint8_t LIGHT_DIM_W2_AT_50_PERCENT = 0;
constexpr uint8_t LIGHT_DIM_W1_AT_50_PERCENT = 0;
constexpr uint8_t LIGHT_DIM_W2_AT_100_PERCENT = 255;
constexpr uint8_t LIGHT_DIM_W1_AT_100_PERCENT = 0;
constexpr uint8_t LIGHT_DIM_COMMAND_MAX_ATTEMPTS = 2;
constexpr uint16_t LIGHT_DIM_WRITE_SETTLE_MS = 2;
constexpr uint16_t LIGHT_DIM_BOOT_SETTLE_MS = 2;

constexpr uint16_t EEPROM_PAGE_SIZE = 32;
constexpr uint16_t EEPROM_SIZE_BYTES = 4096;
constexpr uint16_t EEPROM_RECORD_BASE = 512;

constexpr uint32_t SENSOR_SAMPLE_INTERVAL_MS = 60000UL;
constexpr uint32_t RTC_SERVICE_INTERVAL_MS = 100UL;
constexpr uint32_t HA_PUBLISH_INTERVAL_MS = 60000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000UL;
constexpr uint8_t WIFI_TIMEOUTS_BEFORE_MODULE_RESET = 3;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000UL;
constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 1000UL;
constexpr uint32_t NTP_REQUEST_TIMEOUT_MS = 5000UL;
constexpr uint32_t NTP_RETRY_INTERVAL_MS = 10000UL;
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 86400000UL;
constexpr uint16_t NTP_LOCAL_PORT = 2390;
constexpr uint16_t NTP_SERVER_PORT = 123;
constexpr uint8_t NTP_PACKET_SIZE = 48;
constexpr uint8_t HA_MQTT_ENTITY_LIMIT = 56;

constexpr uint8_t DEFAULT_ALARM1_HOUR = 8;
constexpr uint8_t DEFAULT_ALARM1_MINUTE = 0;
constexpr uint8_t DEFAULT_ALARM1_TARGET_PERCENT = 100;
constexpr uint8_t DEFAULT_ALARM2_HOUR = 20;
constexpr uint8_t DEFAULT_ALARM2_MINUTE = 0;
constexpr uint8_t DEFAULT_ALARM2_TARGET_PERCENT = 0;
constexpr uint16_t DEFAULT_DIM_DURATION_MINUTES = 30;

