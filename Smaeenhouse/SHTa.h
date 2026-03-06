#ifndef SHTA_H
#define SHTA_H

#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cSht3x.h>
#include "Config.h"

class SHTa {
public:
  SHTa();
  SensirionI2cSht3x sensor;
  bool alertTriggers[7];
  void begin();
  void stopMeasurement();
  void startPeriodicMeasurement(Repeatability repeatability, Mps frequency);
  int16_t blockingReadMeasurement(float& aTemperature, float& aHumidity);
  void printMeasurement();
  void readStatusRegister();
  void printStatusRegister();
  void clearStatus();
  void decodeStatusRegister();
  void writeHighLimit(float tempHigh, float humHigh);
  void writeHighClear(float tempHighClear, float humHighClear);
  void writeLowLimit(float tempLow, float humLow);
  void writeLowClear(float tempLowClear, float humLowClear);
  void limitPrinter();
  bool readLimit(uint8_t lsb, uint16_t& raw);
  void printLimit(const char* name, uint16_t raw);


private:
  uint16_t aStatusRegister = 0u;
  char errorMessage[64];
  int16_t error;
  uint8_t crc8(const uint8_t* data, int len);
  uint16_t encodeAlertLimit(float tempC, float humRH);
  float decodeAlertTemp(uint16_t raw);
  float decodeAlertHum(uint16_t raw);
  bool writeLimit(uint8_t lsb, uint16_t raw);
};

#endif