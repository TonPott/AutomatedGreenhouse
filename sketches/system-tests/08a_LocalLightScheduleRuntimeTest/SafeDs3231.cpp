#include "SafeDs3231.h"

#include "Config.h"

namespace {
constexpr uint8_t REG_TIME = 0x00;
constexpr uint8_t REG_ALARM1 = 0x07;
constexpr uint8_t REG_ALARM2 = 0x0B;
constexpr uint8_t REG_CONTROL = 0x0E;
constexpr uint8_t REG_STATUS = 0x0F;
constexpr uint8_t CONTROL_A1IE = 0x01;
constexpr uint8_t CONTROL_A2IE = 0x02;
constexpr uint8_t CONTROL_INTCN = 0x04;
constexpr uint8_t STATUS_A1F = 0x01;
constexpr uint8_t STATUS_A2F = 0x02;
constexpr uint8_t STATUS_OSF = 0x80;
}

bool SafeDs3231::begin(TwoWire& wire) {
  wire_ = &wire;
  uint8_t control = 0;
  if (!readRegister(REG_CONTROL, control)) return false;
  status_ = "ok";
  return true;
}

bool SafeDs3231::readTime(DateTime& value, bool& lostPower) {
  uint8_t raw[7]{};
  uint8_t status = 0;
  if (!readRegisters(REG_TIME, raw, sizeof(raw)) || !readRegister(REG_STATUS, status)) {
    return false;
  }
  if (!validateRawTime(raw)) return fail("invalid_time_data");
  value = DateTime(static_cast<uint16_t>(2000U + fromBcd(raw[6])),
                   fromBcd(raw[5] & 0x1FU),
                   fromBcd(raw[4] & 0x3FU),
                   fromBcd(raw[2] & 0x3FU),
                   fromBcd(raw[1] & 0x7FU),
                   fromBcd(raw[0] & 0x7FU));
  lostPower = (status & STATUS_OSF) != 0;
  status_ = lostPower ? "lost_power" : "ok";
  return true;
}

bool SafeDs3231::writeTime(const DateTime& value) {
  if (value.year() < 2000 || value.year() > 2099 || value.month() < 1 || value.month() > 12 ||
      value.day() < 1 || value.day() > daysInMonth(value.year(), value.month()) ||
      value.hour() > 23 || value.minute() > 59 || value.second() > 59) {
    return fail("invalid_time_write");
  }
  const uint8_t raw[7] = {
      toBcd(value.second()), toBcd(value.minute()), toBcd(value.hour()),
      toBcd(static_cast<uint8_t>(value.dayOfTheWeek() == 0 ? 7 : value.dayOfTheWeek())),
      toBcd(value.day()), toBcd(value.month()), toBcd(static_cast<uint8_t>(value.year() - 2000U))};
  if (!writeRegisters(REG_TIME, raw, sizeof(raw))) return false;
  uint8_t status = 0;
  if (!readRegister(REG_STATUS, status) || !writeRegister(REG_STATUS, status & ~STATUS_OSF)) return false;
  DateTime verified;
  bool lostPower = true;
  if (!readTime(verified, lostPower) || lostPower) return fail("time_verify_failed");
  const uint32_t expected = value.unixtime();
  const uint32_t actual = verified.unixtime();
  const uint32_t delta = expected > actual ? expected - actual : actual - expected;
  if (delta > 1U) return fail("time_verify_failed");
  status_ = "ok";
  return true;
}

bool SafeDs3231::readAlarmFlags(uint8_t& flags) {
  uint8_t status = 0;
  if (!readRegister(REG_STATUS, status)) return false;
  flags = status & (STATUS_A1F | STATUS_A2F);
  status_ = "ok";
  return true;
}

bool SafeDs3231::clearAlarmFlags(uint8_t flags) {
  uint8_t status = 0;
  if (!readRegister(REG_STATUS, status)) return false;
  status &= static_cast<uint8_t>(~(flags & (STATUS_A1F | STATUS_A2F)));
  if (!writeRegister(REG_STATUS, status)) return false;
  uint8_t verified = 0;
  if (!readRegister(REG_STATUS, verified) || (verified & flags & (STATUS_A1F | STATUS_A2F)) != 0) {
    return fail("alarm_clear_verify_failed");
  }
  status_ = "ok";
  return true;
}

bool SafeDs3231::configureAlarms(const DateTime& alarm1, const DateTime& alarm2) {
  uint8_t control = 0;
  uint8_t status = 0;
  if (!readRegister(REG_CONTROL, control) || !readRegister(REG_STATUS, status)) return false;
  const uint8_t disabled = static_cast<uint8_t>((control | CONTROL_INTCN) & ~(CONTROL_A1IE | CONTROL_A2IE));
  if (!writeRegister(REG_CONTROL, disabled)) return false;
  if (!writeRegister(REG_STATUS, status & ~(STATUS_A1F | STATUS_A2F))) return false;

  const uint8_t alarm1Raw[4] = {
      toBcd(alarm1.second()), toBcd(alarm1.minute()), toBcd(alarm1.hour()), toBcd(alarm1.day())};
  const uint8_t alarm2Raw[3] = {toBcd(alarm2.minute()), toBcd(alarm2.hour()), toBcd(alarm2.day())};
  if (!writeRegisters(REG_ALARM1, alarm1Raw, sizeof(alarm1Raw)) ||
      !writeRegisters(REG_ALARM2, alarm2Raw, sizeof(alarm2Raw))) return false;

  uint8_t alarm1Readback[4]{};
  uint8_t alarm2Readback[3]{};
  if (!readRegisters(REG_ALARM1, alarm1Readback, sizeof(alarm1Readback)) ||
      !readRegisters(REG_ALARM2, alarm2Readback, sizeof(alarm2Readback)) ||
      memcmp(alarm1Raw, alarm1Readback, sizeof(alarm1Raw)) != 0 ||
      memcmp(alarm2Raw, alarm2Readback, sizeof(alarm2Raw)) != 0) {
    return fail("alarm_readback_failed");
  }

  if (!readRegister(REG_STATUS, status) ||
      !writeRegister(REG_STATUS, status & ~(STATUS_A1F | STATUS_A2F)) ||
      !writeRegister(REG_CONTROL, static_cast<uint8_t>(disabled | CONTROL_A1IE | CONTROL_A2IE))) return false;
  uint8_t verifiedControl = 0;
  uint8_t verifiedStatus = 0;
  if (!readRegister(REG_CONTROL, verifiedControl) || !readRegister(REG_STATUS, verifiedStatus) ||
      (verifiedControl & (CONTROL_INTCN | CONTROL_A1IE | CONTROL_A2IE)) !=
          (CONTROL_INTCN | CONTROL_A1IE | CONTROL_A2IE) ||
      (verifiedStatus & (STATUS_A1F | STATUS_A2F)) != 0) {
    return fail("alarm_control_verify_failed");
  }
  status_ = "ok";
  return true;
}

bool SafeDs3231::validateRawTime(const uint8_t raw[7]) {
  if ((raw[2] & 0x40U) != 0 || (raw[5] & 0x80U) != 0) return false;
  const uint8_t secondRaw = raw[0] & 0x7FU;
  const uint8_t minuteRaw = raw[1] & 0x7FU;
  const uint8_t hourRaw = raw[2] & 0x3FU;
  const uint8_t dayRaw = raw[4] & 0x3FU;
  const uint8_t monthRaw = raw[5] & 0x1FU;
  if (!validBcd(secondRaw) || !validBcd(minuteRaw) || !validBcd(hourRaw) ||
      !validBcd(dayRaw) || !validBcd(monthRaw) || !validBcd(raw[6])) return false;
  const uint8_t second = fromBcd(secondRaw);
  const uint8_t minute = fromBcd(minuteRaw);
  const uint8_t hour = fromBcd(hourRaw);
  const uint8_t weekday = raw[3] & 0x07U;
  const uint8_t day = fromBcd(dayRaw);
  const uint8_t month = fromBcd(monthRaw);
  const uint16_t year = static_cast<uint16_t>(2000U + fromBcd(raw[6]));
  return second <= 59 && minute <= 59 && hour <= 23 && weekday >= 1 && weekday <= 7 &&
         month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month);
}

uint8_t SafeDs3231::fromBcd(uint8_t value) {
  return static_cast<uint8_t>(value - 6U * (value >> 4));
}

uint8_t SafeDs3231::toBcd(uint8_t value) {
  return static_cast<uint8_t>(value + 6U * (value / 10U));
}

bool SafeDs3231::validBcd(uint8_t value) {
  return (value & 0x0FU) <= 9U && ((value >> 4) & 0x0FU) <= 9U;
}

uint8_t SafeDs3231::daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && ((year % 4U == 0 && year % 100U != 0) || year % 400U == 0)) return 29;
  return days[month - 1U];
}

bool SafeDs3231::readRegisters(uint8_t start, uint8_t* data, size_t length) {
  if (wire_ == nullptr || data == nullptr || length == 0) return fail("not_initialized");
  wire_->beginTransmission(I2C_ADDRESS_DS3231);
  if (wire_->write(start) != 1 || wire_->endTransmission(false) != 0) return fail("write_address_failed");
  if (wire_->requestFrom(I2C_ADDRESS_DS3231, length, true) != length) return fail("short_read");
  for (size_t index = 0; index < length; ++index) {
    if (!wire_->available()) return fail("short_read");
    data[index] = wire_->read();
  }
  return true;
}

bool SafeDs3231::writeRegisters(uint8_t start, const uint8_t* data, size_t length) {
  if (wire_ == nullptr || data == nullptr || length == 0) return fail("not_initialized");
  wire_->beginTransmission(I2C_ADDRESS_DS3231);
  if (wire_->write(start) != 1 || wire_->write(data, length) != length || wire_->endTransmission() != 0) {
    return fail("register_write_failed");
  }
  return true;
}

bool SafeDs3231::readRegister(uint8_t address, uint8_t& value) {
  return readRegisters(address, &value, 1);
}

bool SafeDs3231::writeRegister(uint8_t address, uint8_t value) {
  return writeRegisters(address, &value, 1);
}

bool SafeDs3231::fail(const char* status) {
  status_ = status;
  return false;
}
