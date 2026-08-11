#pragma once

#include <Arduino.h>
#include <JC_EEPROM.h>

#pragma pack(push, 1)
struct ScheduleRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t length;
  uint32_t sequence;
  uint32_t bootCount;
  uint8_t alarm1Hour;
  uint8_t alarm1Minute;
  uint8_t alarm1Target;
  uint8_t alarm2Hour;
  uint8_t alarm2Minute;
  uint8_t alarm2Target;
  uint16_t dimMinutes;
  uint8_t hardPowerOff;
  uint8_t dstState;
  uint32_t lastDstTransitionYmd;
  uint32_t lastAlarm1Ymd;
  uint32_t lastAlarm2Ymd;
  uint16_t checksum;
};
#pragma pack(pop)

class PersistentStore {
public:
  bool begin();
  bool save(ScheduleRecord requested);

  const ScheduleRecord& record() const { return record_; }
  bool isPresent() const { return present_; }
  bool isValid() const { return valid_; }
  bool hasFault() const { return !(present_ && valid_ && readOk_ && writeOk_ && verifyOk_); }
  uint32_t errorCount() const { return errorCount_; }
  uint32_t writeCount() const { return writeCount_; }
  uint32_t skippedCount() const { return skippedCount_; }
  const char* status() const { return status_; }

  static ScheduleRecord defaults();
  static bool validRecord(const ScheduleRecord& record);

private:
  static uint16_t checksum(const ScheduleRecord& record);
  bool read(ScheduleRecord& record);
  bool writeChanged(const ScheduleRecord& record);
  void fail(const char* status);

  JC_EEPROM eeprom_{JC_EEPROM::kbits_32, 1, 32, 0x57};
  ScheduleRecord record_{};
  bool present_ = false;
  bool valid_ = false;
  bool readOk_ = false;
  bool writeOk_ = false;
  bool verifyOk_ = false;
  uint32_t errorCount_ = 0;
  uint32_t writeCount_ = 0;
  uint32_t skippedCount_ = 0;
  const char* status_ = "not_initialized";
};

