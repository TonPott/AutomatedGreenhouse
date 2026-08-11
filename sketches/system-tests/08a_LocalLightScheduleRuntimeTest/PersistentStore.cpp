#include "PersistentStore.h"

#include <Wire.h>

#include "Config.h"

namespace {
constexpr uint32_t RECORD_MAGIC = 0x4C534348UL;  // LSCH
constexpr uint16_t RECORD_VERSION = 1;
}

ScheduleRecord PersistentStore::defaults() {
  ScheduleRecord result{};
  result.magic = RECORD_MAGIC;
  result.version = RECORD_VERSION;
  result.length = sizeof(ScheduleRecord);
  result.sequence = 1;
  result.bootCount = 1;
  result.alarm1Hour = DEFAULT_ALARM1_HOUR;
  result.alarm1Minute = DEFAULT_ALARM1_MINUTE;
  result.alarm1Target = DEFAULT_ALARM1_TARGET_PERCENT;
  result.alarm2Hour = DEFAULT_ALARM2_HOUR;
  result.alarm2Minute = DEFAULT_ALARM2_MINUTE;
  result.alarm2Target = DEFAULT_ALARM2_TARGET_PERCENT;
  result.dimMinutes = DEFAULT_DIM_DURATION_MINUTES;
  result.hardPowerOff = 0;
  result.dstState = 0xFF;
  result.checksum = checksum(result);
  return result;
}

bool PersistentStore::begin() {
  const uint8_t beginResult = eeprom_.begin(JC_EEPROM::twiClock100kHz);
  Wire.beginTransmission(I2C_ADDRESS_AT24C32);
  const uint8_t probeResult = Wire.endTransmission();
  present_ = beginResult == 0 && probeResult == 0;
  if (!present_) {
    fail("not_found");
    record_ = defaults();
    return false;
  }

  ScheduleRecord stored{};
  if (!read(stored)) {
    // A failed boot read must never be followed by a speculative default write.
    record_ = defaults();
    valid_ = false;
    return false;
  }

  if (!validRecord(stored)) {
    record_ = defaults();
    if (!writeChanged(record_)) {
      return false;
    }
  } else {
    record_ = stored;
    record_.bootCount++;
    record_.sequence++;
    record_.checksum = checksum(record_);
    if (!writeChanged(record_)) {
      return false;
    }
  }

  ScheduleRecord verified{};
  if (!read(verified)) {
    verifyOk_ = false;
    valid_ = false;
    return false;
  }
  if (memcmp(&record_, &verified, sizeof(record_)) != 0 || !validRecord(verified)) {
    verifyOk_ = false;
    valid_ = false;
    fail("verify_failed");
    return false;
  }
  record_ = verified;
  valid_ = true;
  readOk_ = true;
  writeOk_ = true;
  verifyOk_ = true;
  status_ = "ok";
  return true;
}

bool PersistentStore::save(ScheduleRecord requested) {
  if (!present_ || !valid_) {
    fail("unavailable");
    return false;
  }
  if (requested.alarm1Hour == record_.alarm1Hour &&
      requested.alarm1Minute == record_.alarm1Minute &&
      requested.alarm1Target == record_.alarm1Target &&
      requested.alarm2Hour == record_.alarm2Hour &&
      requested.alarm2Minute == record_.alarm2Minute &&
      requested.alarm2Target == record_.alarm2Target &&
      requested.dimMinutes == record_.dimMinutes &&
      requested.hardPowerOff == record_.hardPowerOff &&
      requested.dstState == record_.dstState &&
      requested.lastDstTransitionYmd == record_.lastDstTransitionYmd &&
      requested.lastAlarm1Ymd == record_.lastAlarm1Ymd &&
      requested.lastAlarm2Ymd == record_.lastAlarm2Ymd) {
    skippedCount_++;
    return true;
  }

  requested.magic = RECORD_MAGIC;
  requested.version = RECORD_VERSION;
  requested.length = sizeof(ScheduleRecord);
  requested.sequence = record_.sequence + 1U;
  requested.bootCount = record_.bootCount;
  requested.checksum = checksum(requested);

  if (!writeChanged(requested)) {
    return false;
  }
  ScheduleRecord verified{};
  if (!read(verified)) {
    verifyOk_ = false;
    return false;
  }
  if (memcmp(&requested, &verified, sizeof(requested)) != 0 || !validRecord(verified)) {
    verifyOk_ = false;
    fail("verify_failed");
    return false;
  }
  record_ = verified;
  valid_ = true;
  readOk_ = true;
  writeOk_ = true;
  verifyOk_ = true;
  status_ = "ok";
  return true;
}

bool PersistentStore::validRecord(const ScheduleRecord& record) {
  if (record.magic != RECORD_MAGIC || record.version != RECORD_VERSION ||
      record.length != sizeof(ScheduleRecord) || record.checksum != checksum(record)) {
    return false;
  }
  if (record.alarm1Hour > 23 || record.alarm2Hour > 23 ||
      record.alarm1Minute > 59 || record.alarm2Minute > 59 ||
      record.alarm1Target > 100 || record.alarm2Target > 100 ||
      record.dimMinutes > 1440 || record.hardPowerOff > 1 ||
      (record.dstState != 0 && record.dstState != 1 && record.dstState != 0xFF)) {
    return false;
  }
  return record.alarm1Hour != record.alarm2Hour || record.alarm1Minute != record.alarm2Minute;
}

uint16_t PersistentStore::checksum(const ScheduleRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint16_t result = 0x5A5AU;
  for (uint16_t index = 0; index < sizeof(ScheduleRecord) - sizeof(record.checksum); ++index) {
    result = static_cast<uint16_t>((result << 5) | (result >> 11));
    result = static_cast<uint16_t>(result + bytes[index]);
  }
  return result;
}

bool PersistentStore::read(ScheduleRecord& record) {
  const uint8_t result = eeprom_.read(EEPROM_RECORD_BASE,
                                      reinterpret_cast<uint8_t*>(&record),
                                      sizeof(record));
  readOk_ = result == 0;
  if (!readOk_) {
    fail("read_failed");
  }
  return readOk_;
}

bool PersistentStore::writeChanged(const ScheduleRecord& record) {
  ScheduleRecord before{};
  if (!read(before)) {
    return false;
  }
  if (memcmp(&before, &record, sizeof(record)) == 0) {
    skippedCount_++;
    writeOk_ = true;
    return true;
  }
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  for (uint16_t index = 0; index < sizeof(record); ++index) {
    if (reinterpret_cast<const uint8_t*>(&before)[index] == bytes[index]) {
      continue;
    }
    if (eeprom_.update(EEPROM_RECORD_BASE + index, bytes[index]) != 0) {
      writeOk_ = false;
      fail("write_failed");
      return false;
    }
  }
  writeCount_++;
  writeOk_ = true;
  return true;
}

void PersistentStore::fail(const char* status) {
  errorCount_++;
  status_ = status;
}
