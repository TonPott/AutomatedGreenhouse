#pragma once

#include <Arduino.h>

class NetworkManager {
public:
  void begin();
  void update(uint32_t nowMs);

  void setMqttConnected(bool connected, uint32_t nowMs);

  bool isWifiConnected() const;
  bool isMqttConnected() const;
  bool isOnline() const;
  bool isFallbackActive() const;

private:
  void ensureWifiConnected(uint32_t nowMs, bool force);

  bool wifiConnected_ = false;
  bool mqttConnected_ = false;
  bool online_ = false;
  bool fallbackActive_ = false;

  uint32_t lastWifiAttemptMs_ = 0;
  uint32_t lastConnectionOkMs_ = 0;
};
