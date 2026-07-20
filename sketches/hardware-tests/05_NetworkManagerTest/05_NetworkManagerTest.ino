#include <Arduino.h>

#include "NetworkManager.h"

namespace {

NetworkManager networkManager;
uint32_t lastPrintMs = 0;

void printState() {
  Serial.print(F("wifi="));
  Serial.print(networkManager.isWifiConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", mqtt="));
  Serial.print(networkManager.isMqttConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", online="));
  Serial.print(networkManager.isOnline() ? F("YES") : F("NO"));
  Serial.print(F(", fallback="));
  Serial.println(networkManager.isFallbackActive() ? F("ACTIVE") : F("INACTIVE"));
}

void handleSerialCommand(uint32_t nowMs) {
  if (!Serial.available()) {
    return;
  }

  const char command = static_cast<char>(Serial.read());
  if (command == '1') {
    networkManager.setMqttConnected(true, nowMs);
  } else if (command == '0') {
    networkManager.setMqttConnected(false, nowMs);
  }

  printState();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
  }

  networkManager.begin();

  Serial.println(F("NetworkManagerTest ready."));
  Serial.println(F("Commands: 1=mark MQTT connected, 0=mark MQTT disconnected"));
  printState();
}

void loop() {
  const uint32_t nowMs = millis();
  networkManager.update(nowMs);
  handleSerialCommand(nowMs);

  if (nowMs - lastPrintMs >= 2000UL) {
    lastPrintMs = nowMs;
    printState();
  }
}
