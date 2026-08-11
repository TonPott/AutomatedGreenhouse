#pragma once

#include <Arduino.h>
#include <Wire.h>

class Ad5263SafeController {
public:
  struct RdacTarget {
    uint8_t w2 = 0;
    uint8_t w1 = 0;
  };

  void begin(TwoWire& wire);
  bool initializeSafeTarget();
  bool applyBrightness(uint8_t percent);
  bool verifyCurrentTarget();
  bool releaseShutdown();
  bool closeRelay();
  void openRelay();
  void assertShutdown();
  void enforceRelayOpen();

  bool injectNotFound();
  void injectWriteFailure();
  bool injectReadbackMismatch();
  bool recover();

  bool isPresent() const;
  bool isShutdownReleased() const;
  bool isRelayOpen() const;
  bool hasFault() const;
  const char* getFaultReason() const;
  uint8_t getCurrentPercent() const;
  RdacTarget getExpectedTarget() const;
  RdacTarget getReadbackTarget() const;
  uint32_t getApplyCount() const;
  uint32_t getVerifyCount() const;
  uint32_t getRetryCount() const;
  uint32_t getInjectedFaultCount() const;

private:
  static uint8_t clampPercent(uint8_t value);
  RdacTarget brightnessToRdac(uint8_t percent) const;
  bool probeAddress(uint8_t address);
  bool writeTargetOnce(const RdacTarget& target);
  bool verifyTargetOnce(const RdacTarget& target);
  bool writeTargetWithStrategy(const RdacTarget& target);
  bool writeRdacChannel(uint8_t channel, uint8_t value);
  bool selectRdacChannel(uint8_t channel);
  bool readSelectedRdac(uint8_t& value);
  void clearFault();
  void setFault(const char* reason);

  TwoWire* wire_ = nullptr;
  bool present_ = false;
  bool shdnReleased_ = false;
  bool relayOpen_ = true;
  bool fault_ = false;
  const char* faultReason_ = "";
  const char* pendingFailureReason_ = "";
  uint8_t currentPercent_ = 0;
  RdacTarget expectedTarget_{};
  RdacTarget readbackTarget_{};
  uint32_t applyCount_ = 0;
  uint32_t verifyCount_ = 0;
  uint32_t retryCount_ = 0;
  uint32_t injectedFaultCount_ = 0;
};