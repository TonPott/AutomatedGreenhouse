#pragma once

#include <Arduino.h>
#include <string.h>

// Shared retained-topic hygiene for the system tests that intentionally reuse the
// Grow Controller Tests Home Assistant device. Keep this manifest synchronized
// whenever one of Tests 02-09 adds, removes, or renames an entity.
namespace SystemTestHaCleanup {

constexpr uint16_t TEST_02 = 1U << 0;
constexpr uint16_t TEST_03 = 1U << 1;
constexpr uint16_t TEST_04 = 1U << 2;
constexpr uint16_t TEST_05 = 1U << 3;
constexpr uint16_t TEST_06 = 1U << 4;
constexpr uint16_t TEST_07 = 1U << 5;
constexpr uint16_t TEST_08 = 1U << 6;
constexpr uint16_t TEST_09 = 1U << 7;

struct KnownEntity {
  const char* domain;
  const char* id;
  uint16_t activeTests;
};

struct KnownDirectTopic {
  const char* topic;
  uint16_t activeTests;
};

constexpr const char* KNOWN_STATE_PREFIXES[] = {
  "smaeenhouse/test/i2c_passive_baseline/ha",
  "smaeenhouse/test/persistence_rtc_baseline/ha"
};

constexpr KnownDirectTopic KNOWN_DIRECT_TOPICS[] = {
  {"smaeenhouse/test/safe_installed_baseline/status", 0},
  {"smaeenhouse/test/safe_installed_baseline/event", 0},
  {"smaeenhouse/test/i2c_passive_baseline/status", 0},
  {"smaeenhouse/test/i2c_passive_baseline/event", 0},
  {"smaeenhouse/test/sht_hardware_baseline/status", 0},
  {"smaeenhouse/test/persistence_rtc_baseline/status", 0},
  {"smaeenhouse/test/persistence_rtc_baseline/event", 0},
  {"smaeenhouse/test/persistence_rtc_baseline/result", 0},
  {"smaeenhouse/test/sht_fan_closed_loop/status", 0},
  {"smaeenhouse/test/sht_fan_closed_loop/event", 0},
  {"smaeenhouse/test/soil_moisture_calibration/status", TEST_06},
  {"smaeenhouse/test/soil_moisture_calibration/event", TEST_06},
  {"smaeenhouse/test/ad5263_safe_readback/status", TEST_07},
  {"smaeenhouse/test/ad5263_safe_readback/event", TEST_07},
  {"smaeenhouse/test/ad5263_safe_readback/cmd", TEST_07},
  {"smaeenhouse/test/light_relay_manual_ha/status", 0},
  {"smaeenhouse/test/light_relay_manual_ha/event", 0},
  {"smaeenhouse/test/light_relay_manual_ha/cmd", 0},
  {"smaeenhouse/test/arduino_schedule_rtc_light/status", TEST_09},
  {"smaeenhouse/test/arduino_schedule_rtc_light/event", TEST_09},
  {"smaeenhouse/test/arduino_schedule_rtc_light/cmd", TEST_09}
};

constexpr KnownEntity KNOWN_ENTITIES[] = {
  {"binary_sensor", "eeprom_fault", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "fan_fault", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "i2c_ad5263_available", TEST_02},
  {"binary_sensor", "i2c_at24c32_available", TEST_02},
  {"binary_sensor", "i2c_ds3231_available", TEST_02},
  {"binary_sensor", "i2c_rtc_alarm_line", TEST_02},
  {"binary_sensor", "i2c_scl_high", TEST_02},
  {"binary_sensor", "i2c_sda_high", TEST_02},
  {"binary_sensor", "i2c_sht31_available", TEST_02},
  {"binary_sensor", "i2c_sht_alert_line", TEST_02},
  {"binary_sensor", "i2c_tsl25911_available", TEST_02},
  {"binary_sensor", "light_fault", TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "persistence_rtc_alarm1_configured", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "persistence_rtc_alarm2_configured", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "persistence_rtc_eeprom_available", TEST_04},
  {"binary_sensor", "persistence_rtc_eeprom_last_transfer_ok", TEST_04},
  {"binary_sensor", "persistence_rtc_eeprom_read_ok", TEST_04},
  {"binary_sensor", "persistence_rtc_eeprom_record_valid", TEST_04},
  {"binary_sensor", "persistence_rtc_eeprom_recovery_pending", TEST_04},
  {"binary_sensor", "persistence_rtc_eeprom_verify_ok", TEST_04},
  {"binary_sensor", "persistence_rtc_eeprom_write_ok", TEST_04},
  {"binary_sensor", "persistence_rtc_fan_safe", TEST_02 | TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "persistence_rtc_lost_power", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "persistence_rtc_relay_safe", TEST_02 | TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "persistence_rtc_shdn_safe", TEST_02 | TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "rtc_fault", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "sht_address_45", TEST_03},
  {"binary_sensor", "sht_alert_line", TEST_03 | TEST_05},
  {"binary_sensor", "sht_fault", TEST_03 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"binary_sensor", "sht_interrupt_attached", TEST_03 | TEST_05},
  {"binary_sensor", "sht_limits_ok", TEST_03},
  {"binary_sensor", "sht_measurement_ok", TEST_03},
  {"binary_sensor", "sht_status_ok", TEST_03},
  {"button", "read_soil_raw_value", TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"button", "run_sht_limit_round_trip", TEST_03},
  {"button", "verify_persistence_record", TEST_04},
  {"light", "grow_light", TEST_08 | TEST_09},
  {"number", "hum_high_clear", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "hum_high_set", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "hum_low_clear", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "hum_low_set", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "light_dim_minutes", TEST_09},
  {"number", "light_off_target_percent", TEST_09},
  {"number", "light_off_time_minutes", TEST_09},
  {"number", "light_on_target_percent", TEST_09},
  {"number", "light_on_time_minutes", TEST_09},
  {"number", "soil_air", TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "soil_depth_mm", TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "soil_water", TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "temp_high_clear", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "temp_high_set", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "temp_low_clear", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"number", "temp_low_set", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "ad5263_brightness_percent", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "ad5263_expected_w1", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "ad5263_expected_w2", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "ad5263_readback_w1", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "ad5263_readback_w2", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "ad5263_step_index", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "ad5263_test_step", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "fan_rpm", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "fan_tach_pulses", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "humidity", TEST_03 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "i2c_ad5263_consecutive_errors", TEST_02},
  {"sensor", "i2c_ad5263_probe_errors", TEST_02},
  {"sensor", "i2c_at24c32_consecutive_errors", TEST_02},
  {"sensor", "i2c_at24c32_probe_errors", TEST_02},
  {"sensor", "i2c_bus_status", TEST_02},
  {"sensor", "i2c_bus_stuck_count", TEST_02},
  {"sensor", "i2c_ds3231_consecutive_errors", TEST_02},
  {"sensor", "i2c_ds3231_probe_errors", TEST_02},
  {"sensor", "i2c_last_error", TEST_02},
  {"sensor", "i2c_ota_gap_violations", TEST_02},
  {"sensor", "i2c_probe_phase", TEST_02},
  {"sensor", "i2c_reset_cause", TEST_02},
  {"sensor", "i2c_scan_sequence", TEST_02},
  {"sensor", "i2c_sht31_consecutive_errors", TEST_02},
  {"sensor", "i2c_sht31_probe_errors", TEST_02},
  {"sensor", "i2c_test_step", TEST_02},
  {"sensor", "i2c_test_step_index", TEST_02},
  {"sensor", "i2c_tsl25911_consecutive_errors", TEST_02},
  {"sensor", "i2c_tsl25911_probe_errors", TEST_02},
  {"sensor", "i2c_uptime_seconds", TEST_02},
  {"sensor", "i2c_wifi_joins", TEST_02},
  {"sensor", "i2c_wifi_module_resets", TEST_02},
  {"sensor", "i2c_wifi_timeouts", TEST_02},
  {"sensor", "light_alarm1_next_epoch", TEST_09},
  {"sensor", "light_alarm2_next_epoch", TEST_09},
  {"sensor", "light_fault_reason", TEST_07 | TEST_08 | TEST_09},
  {"sensor", "light_schedule_event", TEST_09},
  {"sensor", "light_schedule_progress_percent", TEST_09},
  {"sensor", "light_schedule_state", TEST_09},
  {"sensor", "persistence_rtc_alarm1_seen", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_alarm2_seen", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_alarm_clears", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_alarm_isr_seen", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_eeprom_boot_count", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_eeprom_checksum", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_eeprom_consecutive_failures", TEST_04},
  {"sensor", "persistence_rtc_eeprom_last_error", TEST_04},
  {"sensor", "persistence_rtc_eeprom_last_write_bytes", TEST_04},
  {"sensor", "persistence_rtc_eeprom_last_write_ranges", TEST_04},
  {"sensor", "persistence_rtc_eeprom_reads", TEST_04},
  {"sensor", "persistence_rtc_eeprom_record_status", TEST_04},
  {"sensor", "persistence_rtc_eeprom_recoveries", TEST_04},
  {"sensor", "persistence_rtc_eeprom_recovery_attempts", TEST_04},
  {"sensor", "persistence_rtc_eeprom_sequence", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_eeprom_skipped_writes", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_eeprom_transport_status", TEST_04},
  {"sensor", "persistence_rtc_eeprom_write_bytes", TEST_04},
  {"sensor", "persistence_rtc_eeprom_write_ranges", TEST_04},
  {"sensor", "persistence_rtc_eeprom_writes", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_epoch", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_ota_gap_violations", TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_result", TEST_04},
  {"sensor", "persistence_rtc_runtime_attempts", TEST_04},
  {"sensor", "persistence_rtc_runtime_failures", TEST_04},
  {"sensor", "persistence_rtc_runtime_successes", TEST_04},
  {"sensor", "persistence_rtc_test_step", TEST_04},
  {"sensor", "persistence_rtc_test_step_index", TEST_04},
  {"sensor", "persistence_rtc_uptime_seconds", TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_wifi_joins", TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_wifi_module_resets", TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "persistence_rtc_wifi_timeouts", TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "sht_address_probe_failures", TEST_03},
  {"sensor", "sht_alert_interrupts", TEST_05},
  {"sensor", "sht_consecutive_failures", TEST_03},
  {"sensor", "sht_diagnostic", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "sht_last_error", TEST_03},
  {"sensor", "sht_limit_errors", TEST_03},
  {"sensor", "sht_limit_high_clear_raw", TEST_03},
  {"sensor", "sht_limit_high_set_raw", TEST_03},
  {"sensor", "sht_limit_low_clear_raw", TEST_03},
  {"sensor", "sht_limit_low_set_raw", TEST_03},
  {"sensor", "sht_max_consecutive_failures", TEST_03},
  {"sensor", "sht_measurement_errors", TEST_03 | TEST_05},
  {"sensor", "sht_recoveries", TEST_03},
  {"sensor", "sht_recovery_attempts", TEST_03},
  {"sensor", "sht_recovery_failures", TEST_03},
  {"sensor", "sht_round_trip_attempts", TEST_03},
  {"sensor", "sht_round_trip_failures", TEST_03},
  {"sensor", "sht_round_trip_result", TEST_03},
  {"sensor", "sht_round_trip_successes", TEST_03},
  {"sensor", "sht_status_errors", TEST_03 | TEST_05},
  {"sensor", "sht_status_flags", TEST_03},
  {"sensor", "sht_status_register", TEST_03},
  {"sensor", "sht_test_step", TEST_03},
  {"sensor", "sht_test_step_index", TEST_03},
  {"sensor", "sht_threshold_result", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "sketch_identity", TEST_02 | TEST_03 | TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "sketch_name", 0},
  {"sensor", "sketch_version", 0},
  {"sensor", "soil_moisture_percent", TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "soil_moisture_raw", TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "temperature", TEST_03 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"sensor", "test_event", TEST_05 | TEST_08},
  {"switch", "fan", TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"switch", "fan_auto_mode", TEST_04 | TEST_05 | TEST_06 | TEST_07 | TEST_08 | TEST_09},
  {"switch", "light_auto_mode", TEST_08 | TEST_09},
  {"switch", "light_hard_power_off", TEST_08 | TEST_09},
};

class CleanupCursor {
 public:
  explicit CleanupCursor(uint16_t currentTest) : currentTest_(currentTest) {}

  template <typename MqttType>
  bool service(MqttType& mqtt,
               const char* discoveryPrefix,
               const char* deviceId,
               const char* currentStatePrefix) {
    if (complete_ || !mqtt.isConnected()) {
      return false;
    }

    char topic[192];
    while (directIndex_ < directCount()) {
      const KnownDirectTopic& direct = KNOWN_DIRECT_TOPICS[directIndex_];
      if ((direct.activeTests & currentTest_) != 0U) {
        directIndex_++;
        continue;
      }
      if (mqtt.publish(direct.topic, "", true)) {
        directIndex_++;
      }
      return false;
    }

    while (entityIndex_ < entityCount()) {
      const KnownEntity& entity = KNOWN_ENTITIES[entityIndex_];
      const bool active = (entity.activeTests & currentTest_) != 0U;

      if (entityPhase_ == 0U) {
        entityPhase_ = 1U;
        if (active) {
          continue;
        }
        snprintf(topic,
                 sizeof(topic),
                 "%s/%s/%s/%s/config",
                 discoveryPrefix,
                 entity.domain,
                 deviceId,
                 entity.id);
        if (!mqtt.publish(topic, "", true)) {
          entityPhase_ = 0U;
        }
        return false;
      }

      const uint8_t prefixIndex = entityPhase_ - 1U;
      if (prefixIndex < statePrefixCount()) {
        entityPhase_++;
        const char* statePrefix = KNOWN_STATE_PREFIXES[prefixIndex];
        if (active && strcmp(statePrefix, currentStatePrefix) == 0) {
          continue;
        }
        snprintf(topic,
                 sizeof(topic),
                 "%s/%s/%s/stat_t",
                 statePrefix,
                 deviceId,
                 entity.id);
        if (!mqtt.publish(topic, "", true)) {
          entityPhase_--;
        }
        return false;
      }

      entityPhase_ = 0U;
      entityIndex_++;
    }

    complete_ = true;
    return true;
  }

  bool isComplete() const { return complete_; }

 private:
  static constexpr uint16_t directCount() {
    return sizeof(KNOWN_DIRECT_TOPICS) / sizeof(KNOWN_DIRECT_TOPICS[0]);
  }

  static constexpr uint16_t entityCount() {
    return sizeof(KNOWN_ENTITIES) / sizeof(KNOWN_ENTITIES[0]);
  }

  static constexpr uint8_t statePrefixCount() {
    return sizeof(KNOWN_STATE_PREFIXES) / sizeof(KNOWN_STATE_PREFIXES[0]);
  }

  uint16_t currentTest_;
  uint16_t directIndex_ = 0;
  uint16_t entityIndex_ = 0;
  uint8_t entityPhase_ = 0;
  bool complete_ = false;
};

}  // namespace SystemTestHaCleanup
