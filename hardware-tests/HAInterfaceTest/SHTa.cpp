#include "SHTa.h"

#include "Config.h"

// MSB
#define SHT31_ALERT_READ 0xE1
#define SHT31_ALERT_WRITE 0x61
// LSB
#define SHT31_ALERT_RHS 0x1F
#define SHT31_ALERT_RHC 0x14
#define SHT31_ALERT_RLC 0x09
#define SHT31_ALERT_RLS 0x02
#define SHT31_ALERT_WHS 0x1D
#define SHT31_ALERT_WHC 0x16
#define SHT31_ALERT_WLC 0x0B
#define SHT31_ALERT_WLS 0x00

SHTa::SHTa()
    : aStatusRegister(0), error(NO_ERROR), lastTemperature_(NAN), lastHumidity_(NAN) {
  clearDecodedFlags();
}

void SHTa::begin() {
  sensor.begin(Wire, SHT30_I2C_ADDR_45);
  sensor.stopMeasurement();
  delay(1);
  sensor.softReset();
  delay(10);
}

void SHTa::stopMeasurement() {
  sensor.stopMeasurement();
}

void SHTa::startPeriodicMeasurement(Repeatability repeatability, Mps frequency) {
  error = sensor.startPeriodicMeasurement(repeatability, frequency);
  if (error != NO_ERROR) {
    Serial.print(F("SHT startPeriodicMeasurement() failed: "));
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
  }
}

int16_t SHTa::blockingReadMeasurement(float& aTemperature, float& aHumidity) {
  float localTemp = 0.0f;
  float localHum = 0.0f;

  error = sensor.blockingReadMeasurement(localTemp, localHum);
  if (error != NO_ERROR) {
    Serial.print(F("SHT blockingReadMeasurement() failed: "));
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
    return error;
  }

  lastTemperature_ = localTemp;
  lastHumidity_ = localHum;

  aTemperature = localTemp;
  aHumidity = localHum;
  return NO_ERROR;
}

bool SHTa::readMeasurement(float& temperature, float& humidity) {
  return blockingReadMeasurement(temperature, humidity) == NO_ERROR;
}

void SHTa::printMeasurement() {
  float aTemperature = 0.0f;
  float aHumidity = 0.0f;
  if (readMeasurement(aTemperature, aHumidity)) {
    Serial.print(F("Temperature: "));
    Serial.print(aTemperature, 2);
    Serial.print(F(" C\tHumidity: "));
    Serial.println(aHumidity, 1);
  }
}

void SHTa::readStatusRegister() {
  error = sensor.readStatusRegister(aStatusRegister);
  if (error != NO_ERROR) {
    Serial.print(F("SHT readStatusRegister() failed: "));
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
  }
}

void SHTa::printStatusRegister() {
  readStatusRegister();

  Serial.print(F("Status-Reg: 0x"));
  Serial.println(aStatusRegister, HEX);
  Serial.print(F("Alert pending: "));
  Serial.println((aStatusRegister & (1U << 15)) ? F("YES") : F("NO"));
  Serial.print(F("Heater ON: "));
  Serial.println((aStatusRegister & (1U << 13)) ? F("YES") : F("NO"));
  Serial.print(F("RH tracking alert: "));
  Serial.println((aStatusRegister & (1U << 11)) ? F("YES") : F("NO"));
  Serial.print(F("Temp tracking alert: "));
  Serial.println((aStatusRegister & (1U << 10)) ? F("YES") : F("NO"));
  Serial.print(F("System reset detect: "));
  Serial.println((aStatusRegister & (1U << 4)) ? F("YES") : F("NO"));
  Serial.print(F("Command error: "));
  Serial.println((aStatusRegister & (1U << 1)) ? F("YES") : F("NO"));
  Serial.print(F("Write CRC error: "));
  Serial.println((aStatusRegister & (1U << 0)) ? F("YES") : F("NO"));
  Serial.println();
}

void SHTa::clearStatus() {
  Wire.beginTransmission(SHT30_I2C_ADDR_45);
  Wire.write(0x30);
  Wire.write(0x41);
  Wire.endTransmission();
  delayMicroseconds(50);
}

void SHTa::decodeStatusRegister() {
  readStatusRegister();

  // Alert pending
  alertTriggers[0] = (aStatusRegister & (1U << 15)) != 0;
  // Heater ON
  alertTriggers[1] = (aStatusRegister & (1U << 13)) != 0;
  // Humidity tracking
  alertTriggers[2] = (aStatusRegister & (1U << 11)) != 0;
  // Temperature tracking
  alertTriggers[3] = (aStatusRegister & (1U << 10)) != 0;
  // System reset detected
  alertTriggers[4] = (aStatusRegister & (1U << 4)) != 0;
  // Command error
  alertTriggers[5] = (aStatusRegister & (1U << 1)) != 0;
  // Write CRC error
  alertTriggers[6] = (aStatusRegister & (1U << 0)) != 0;
}

void SHTa::applyThresholdConfig(const PersistentConfigData& cfg) {
  writeHighLimit(cfg.tempHighSet, cfg.humHighSet);
  writeHighClear(cfg.tempHighClear, cfg.humHighClear);
  writeLowLimit(cfg.tempLowSet, cfg.humLowSet);
  writeLowClear(cfg.tempLowClear, cfg.humLowClear);
}

bool SHTa::hasTempAlert() const {
  return alertTriggers[3];
}

bool SHTa::hasHumAlert() const {
  return alertTriggers[2];
}

bool SHTa::hasSensorReset() const {
  return alertTriggers[4];
}

void SHTa::clearDecodedFlags() {
  for (bool& trigger : alertTriggers) {
    trigger = false;
  }
}

float SHTa::getLastTemperature() const {
  return lastTemperature_;
}

float SHTa::getLastHumidity() const {
  return lastHumidity_;
}

void SHTa::printLimit(const char* name, uint16_t raw) {
  Serial.print(name);
  Serial.print(F(": T="));
  Serial.print(decodeAlertTemp(raw), 2);
  Serial.print(F(" C, RH="));
  Serial.print(decodeAlertHum(raw), 1);
  Serial.println(F(" %"));
}

uint8_t SHTa::crc8(const uint8_t* data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1) ^ 0x31U) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

uint16_t SHTa::encodeAlertLimit(float tempC, float humRH) {
  if (tempC < -45.0f) {
    tempC = -45.0f;
  }
  if (tempC > 130.0f) {
    tempC = 130.0f;
  }
  if (humRH < 0.0f) {
    humRH = 0.0f;
  }
  if (humRH > 100.0f) {
    humRH = 100.0f;
  }

  const uint16_t tempRaw = static_cast<uint16_t>((tempC + 45.0f) * 65535.0f / 175.0f);
  const uint16_t humRaw = static_cast<uint16_t>(humRH * 65535.0f / 100.0f);

  const uint16_t temp9 = tempRaw >> 7;
  const uint8_t hum7 = humRaw >> 9;

  return static_cast<uint16_t>((static_cast<uint16_t>(hum7) << 9) | temp9);
}

float SHTa::decodeAlertTemp(uint16_t raw) {
  const uint16_t temp9 = raw & 0x01FFU;
  return (static_cast<float>(temp9) * 175.0f / 511.0f) - 45.0f;
}

float SHTa::decodeAlertHum(uint16_t raw) {
  const uint16_t hum7 = (raw >> 9) & 0x7FU;
  return static_cast<float>(hum7) * 100.0f / 127.0f;
}

bool SHTa::writeLimit(uint8_t lsb, uint16_t raw) {
  const uint8_t buf[2] = {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)};
  const uint8_t crc = crc8(buf, 2);

  Wire.beginTransmission(SHT30_I2C_ADDR_45);
  Wire.write(SHT31_ALERT_WRITE);
  Wire.write(lsb);
  Wire.write(buf[0]);
  Wire.write(buf[1]);
  Wire.write(crc);
  return Wire.endTransmission() == 0;
}

void SHTa::writeHighLimit(float tempHigh, float humHigh) {
  writeLimit(SHT31_ALERT_WHS, encodeAlertLimit(tempHigh, humHigh));
}

void SHTa::writeHighClear(float tempHighClear, float humHighClear) {
  writeLimit(SHT31_ALERT_WHC, encodeAlertLimit(tempHighClear, humHighClear));
}

void SHTa::writeLowLimit(float tempLow, float humLow) {
  writeLimit(SHT31_ALERT_WLS, encodeAlertLimit(tempLow, humLow));
}

void SHTa::writeLowClear(float tempLowClear, float humLowClear) {
  writeLimit(SHT31_ALERT_WLC, encodeAlertLimit(tempLowClear, humLowClear));
}

bool SHTa::readLimit(uint8_t lsb, uint16_t& raw) {
  Wire.beginTransmission(SHT30_I2C_ADDR_45);
  Wire.write(SHT31_ALERT_READ);
  Wire.write(lsb);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delayMicroseconds(50);

  if (Wire.requestFrom(static_cast<uint8_t>(SHT30_I2C_ADDR_45), static_cast<uint8_t>(3)) != 3) {
    return false;
  }

  uint8_t b[2];
  b[0] = Wire.read();
  b[1] = Wire.read();
  const uint8_t crc = Wire.read();

  if (crc8(b, 2) != crc) {
    return false;
  }

  raw = (static_cast<uint16_t>(b[0]) << 8) | b[1];
  return true;
}

void SHTa::limitPrinter() {
  uint16_t raw = 0;

  if (readLimit(SHT31_ALERT_RHS, raw)) {
    printLimit("HIGH SET", raw);
  }
  if (readLimit(SHT31_ALERT_RHC, raw)) {
    printLimit("HIGH CLR", raw);
  }
  if (readLimit(SHT31_ALERT_RLS, raw)) {
    printLimit("LOW SET", raw);
  }
  if (readLimit(SHT31_ALERT_RLC, raw)) {
    printLimit("LOW CLR", raw);
  }
}

