#pragma once

#include <Arduino.h>
#include <Wire.h>

class Ad5263Controller {
public:
  struct Target {
    uint8_t w2;
    uint8_t w1;
  };

  void begin(TwoWire& wire);
  bool initializeSafe();
  bool apply(uint8_t percent, bool allowPower);
  bool applyWhilePowered(uint8_t percent);
  bool verifyCurrent();
  void hardPowerOff();
  bool releaseHardPowerOff(uint8_t percent);
  void forceSafeOff();

  bool isPresent() const { return present_; }
  bool hasFault() const { return fault_; }
  bool isRelayOpen() const { return relayOpen_; }
  bool isShutdownReleased() const { return shdnReleased_; }
  uint8_t currentPercent() const { return currentPercent_; }
  uint32_t errorCount() const { return errorCount_; }
  const char* status() const { return status_; }

private:
  static uint8_t clampPercent(uint8_t value);
  Target mapTarget(uint8_t percent) const;
  bool probe();
  bool writeAndVerify(const Target& target);
  bool writeChannel(uint8_t channel, uint8_t value);
  bool readChannel(uint8_t channel, uint8_t& value);
  void openRelay();
  bool closeRelay();
  void assertShutdown();
  void releaseShutdown();
  void setFault(const char* status, bool countI2cError = false);

  TwoWire* wire_ = nullptr;
  bool present_ = false;
  bool fault_ = false;
  bool relayOpen_ = true;
  bool shdnReleased_ = false;
  uint8_t currentPercent_ = 0;
  Target expected_{0, 255};
  uint32_t errorCount_ = 0;
  const char* status_ = "not_initialized";
};

