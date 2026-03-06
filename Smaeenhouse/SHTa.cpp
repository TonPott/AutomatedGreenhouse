#include "SHTa.h"
#include "Config.h"

// MSB
#define SHT31_Alert_Read 0xE1
#define SHT31_Alert_Write 0x61
// LSB
#define SHT31_Alert_rHs 0x1F
#define SHT31_Alert_rHc 0x14
#define SHT31_Alert_rLc 0x09
#define SHT31_Alert_rLs 0x02
#define SHT31_Alert_wHs 0x1D
#define SHT31_Alert_wHc 0x16
#define SHT31_Alert_wLc 0x0B
#define SHT31_Alert_wLs 0x00

// Interrupts alert, heater, humidity, temperature, reset, command, crc
bool alertTriggers[7] = { 0, 0, 0, 0, 0, 0, 0 };

SHTa::SHTa()
  : aStatusRegister(0), error(NO_ERROR) {
}

void SHTa::begin() {
  sensor.begin(Wire, SHT30_I2C_ADDR_45);
  sensor.stopMeasurement();
  delay(1);
  sensor.softReset();
  delay(100);
}

void SHTa::stopMeasurement() {
  sensor.stopMeasurement();
}
void SHTa::startPeriodicMeasurement(Repeatability repeatability, Mps frequency) {
  error = sensor.startPeriodicMeasurement(repeatability,
                                          frequency);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }
}

int16_t SHTa::blockingReadMeasurement(float& aTemperature, float& aHumidity) {
  float localTemp = 0;
  float localHumi = 0;
  error = sensor.blockingReadMeasurement(localTemp, localHumi);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute blockingReadMeasurement(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return error;
  }
  aTemperature = localTemp;
  aHumidity = localHumi;
}

void SHTa::printMeasurement() {
  float aTemperature = 0.0;
  float aHumidity = 0.0;
  sensor.blockingReadMeasurement(aTemperature, aHumidity);
  Serial.print("Temperature: ");
  Serial.print(aTemperature);
  Serial.print("\t");
  Serial.print("Humidity: ");
  Serial.println(aHumidity);
}

void SHTa::readStatusRegister() {
  error = sensor.readStatusRegister(aStatusRegister);
  if (error != NO_ERROR) {
    Serial.print("Error trying to execute readStatusRegister(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }
}

void SHTa::printStatusRegister() {
  readStatusRegister();
  Serial.print(F("Status-Reg: 0x"));
  Serial.println(aStatusRegister, HEX);

  Serial.print(F("  Alert pending       : "));
  Serial.println(aStatusRegister & (1 << 15) ? F("YES") : F("no"));

  Serial.print(F("  Heater ON           : "));
  Serial.println(aStatusRegister & (1 << 13) ? F("YES") : F("no"));

  Serial.print(F("  RH tracking alert   : "));
  Serial.println(aStatusRegister & (1 << 11) ? F("YES") : F("no"));

  Serial.print(F("  Temp tracking alert : "));
  Serial.println(aStatusRegister & (1 << 10) ? F("YES") : F("no"));

  Serial.print(F("  System reset detect : "));
  Serial.println(aStatusRegister & (1 << 4) ? F("YES") : F("no"));

  Serial.print(F("  Command error       : "));
  Serial.println(aStatusRegister & (1 << 1) ? F("YES") : F("no"));

  Serial.print(F("  Write CRC error     : "));
  Serial.println(aStatusRegister & (1 << 0) ? F("YES") : F("no"));

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
  aStatusRegister&(1 << 15) ? alertTriggers[0] = 1 : alertTriggers[0] = 0;
  // Heater ON
  aStatusRegister&(1 << 13) ? alertTriggers[1] = 1 : alertTriggers[1] = 0;
  // Humidity
  aStatusRegister&(1 << 11) ? alertTriggers[2] = 1 : alertTriggers[2] = 0;
  // Temperature
  aStatusRegister&(1 << 10) ? alertTriggers[3] = 1 : alertTriggers[3] = 0;
  // System Reset
  aStatusRegister&(1 << 4) ? alertTriggers[4] = 1 : alertTriggers[4] = 0;
  // Command error
  aStatusRegister&(1 << 1) ? alertTriggers[5] = 1 : alertTriggers[5] = 0;
  // Write CRC error
  aStatusRegister&(1 << 0) ? alertTriggers[6] = 1 : alertTriggers[6] = 0;
}

// ---------- Print helper ----------
void SHTa::printLimit(const char* name, uint16_t raw) {
  Serial.print(name);
  Serial.print(":  T=");
  Serial.print(decodeAlertTemp(raw), 2);
  Serial.print(" °C, RH=");
  Serial.print(decodeAlertHum(raw), 1);
  Serial.println(" %");
}

// ---------- CRC8 (Polynom 0x31) ----------
uint8_t SHTa::crc8(const uint8_t* data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
  }
  return crc;
}

uint16_t SHTa::encodeAlertLimit(float tempC, float humRH) {
  // Clamp
  if (tempC < -45) tempC = -45;
  if (tempC > 130) tempC = 130;
  if (humRH < 0) humRH = 0;
  if (humRH > 100) humRH = 100;
  // --- Convert physical values to 16-bit raw sensor values
  uint16_t tempRaw = (uint16_t)((tempC + 45.0f) * 65535.0f / 175.0f);
  uint16_t humRaw = (uint16_t)(humRH * 65535.0f / 100.0f);

  // --- Extract MSBs according to datasheet
  uint16_t temp9 = tempRaw >> 7;  // keep 9 MSBs
  uint8_t hum7 = humRaw >> 9;     // keep 7 MSBs

  // --- Combine into 16-bit alert limit register value
  return (hum7 << 9) | temp9;
}

// ---------- Decode ----------
float SHTa::decodeAlertTemp(uint16_t raw) {
  uint16_t temp9 = (raw & 0x01FF);                           // Extract temperature (9 bits)
  float tempC = (float(temp9) * 175.0f / 65535.0f) - 45.0f;  // Convert back to temperature
}
float SHTa::decodeAlertHum(uint16_t raw) {
  uint16_t humRaw = (raw >> 9);                       // Extract humidity (7 bits)
  float humRH = (float(humRaw) * 100.0f / 65535.0f);  // Convert back to humidity
}


// ---------- Write limit ----------
bool SHTa::writeLimit(uint8_t lsb, uint16_t raw) {
  uint8_t buf[2] = { uint8_t(raw >> 8), uint8_t(raw & 0xFF) };
  uint8_t crc = crc8(buf, 2);

  Wire.beginTransmission(SHT30_I2C_ADDR_45);
  Wire.write(SHT31_Alert_Write);
  Wire.write(lsb);
  Wire.write(buf[0]);
  Wire.write(buf[1]);
  Wire.write(crc);
  return Wire.endTransmission() == 0;
}
void SHTa::writeHighLimit(float tempHigh, float humHigh) {
  writeLimit(SHT31_Alert_wHs, encodeAlertLimit(tempHigh, humHigh));
}
void SHTa::writeHighClear(float tempHighClear, float humHighClear) {
  writeLimit(SHT31_Alert_wHc, encodeAlertLimit(tempHighClear, humHighClear));
}
void SHTa::writeLowLimit(float tempLow, float humLow) {
  writeLimit(SHT31_Alert_wLs, encodeAlertLimit(tempLow, humLow));
}
void SHTa::writeLowClear(float tempLowClear, float humLowClear) {
  writeLimit(SHT31_Alert_wLc, encodeAlertLimit(tempLowClear, humLowClear));
}

// ---------- Read limit ----------
bool SHTa::readLimit(uint8_t lsb, uint16_t& raw) {
  Wire.beginTransmission(SHT30_I2C_ADDR_45);
  Wire.write(SHT31_Alert_Read);
  Wire.write(lsb);
  if (Wire.endTransmission() != 0) return false;

  delayMicroseconds(50);

  if (Wire.requestFrom((uint8_t)SHT30_I2C_ADDR_45, (uint8_t)3) != 3) return false;

  uint8_t b[2];
  b[0] = Wire.read();
  b[1] = Wire.read();
  uint8_t crc = Wire.read();

  if (crc8(b, 2) != crc) return false;

  raw = (uint16_t(b[0]) << 8) | b[1];
  return true;
}
void SHTa::limitPrinter() {
  uint16_t raw;

  if (readLimit(SHT31_Alert_rHs, raw)) printLimit("HIGH SET ", raw);
  if (readLimit(SHT31_Alert_rHc, raw)) printLimit("HIGH CLR ", raw);
  if (readLimit(SHT31_Alert_rLs, raw)) printLimit("LOW  SET ", raw);
  if (readLimit(SHT31_Alert_rLc, raw)) printLimit("LOW  CLR ", raw);
}