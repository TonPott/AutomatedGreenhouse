#pragma once

// -------- Pins --------
#define PIN_SHT_ALERT      7
#define PIN_FAN_SWITCH     2
#define PIN_FAN_TACH       A1

#define PIN_LIGHT_PWM      4
#define PIN_LIGHT_POWER    3

#define PIN_SOIL_SENSOR    A0

// -------- Timing --------
#define TEMP_PUBLISH_INTERVAL_MS   60000UL
#define SOIL_PUBLISH_INTERVAL_MS   1800000UL
#define FAN_RPM_INTERVAL_MS        30000UL

// -------- Light PWM --------
#define PWM_MIN_ACTIVE     0
#define PWM_MAX_ACTIVE     120
#define PWM_OFF_THRESHOLD  160
#define DIM_STEP           5
#define DIM_INTERVAL_MS    100
