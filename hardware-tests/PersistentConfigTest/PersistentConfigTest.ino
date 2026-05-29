#include <Arduino.h>

#include "Config.h"
#include "PersistentConfig.h"

namespace {

PersistentConfigManager configManager;
PersistentConfigData cfg{};

void printConfig(const PersistentConfigData& data) {
  Serial.println(F("Persistent config snapshot"));
  Serial.print(F("schemaVersion: "));
  Serial.println(data.schemaVersion);
  Serial.print(F("tempHighSet: "));
  Serial.println(data.tempHighSet, 1);
  Serial.print(F("humHighSet: "));
  Serial.println(data.humHighSet, 1);
  Serial.print(F("lightOnTimeMinutes: "));
  Serial.println(data.lightOnTimeMinutes);
  Serial.print(F("lightOffTimeMinutes: "));
  Serial.println(data.lightOffTimeMinutes);
  Serial.print(F("defaultLightDimMinutes: "));
  Serial.println(data.defaultLightDimMinutes);
  Serial.print(F("fanAutoMode: "));
  Serial.println(data.fanAutoMode ? F("ON") : F("OFF"));
  Serial.print(F("lightAutoMode: "));
  Serial.println(data.lightAutoMode ? F("ON") : F("OFF"));
  Serial.print(F("lightFallbackMode: "));
  Serial.println(data.lightFallbackMode);
  Serial.print(F("soilAir: "));
  Serial.println(data.soilAir);
  Serial.print(F("soilWater: "));
  Serial.println(data.soilWater);
  Serial.print(F("soilDepthMm: "));
  Serial.println(data.soilDepthMm);
  Serial.println();
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char command = static_cast<char>(Serial.read());
  if (command == 'p') {
    configManager.load(cfg);
    printConfig(cfg);
  } else if (command == 'a') {
    cfg.lightAutoMode = !cfg.lightAutoMode;
    configManager.saveIfChanged(cfg);
    Serial.println(F("Toggled lightAutoMode and saved."));
    printConfig(cfg);
  } else if (command == 'f') {
    cfg.fanAutoMode = !cfg.fanAutoMode;
    configManager.saveIfChanged(cfg);
    Serial.println(F("Toggled fanAutoMode and saved."));
    printConfig(cfg);
  } else if (command == 'd') {
    configManager.applyDefaults(cfg);
    configManager.save(cfg);
    Serial.println(F("Restored defaults and saved."));
    printConfig(cfg);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {
  }

  const bool hadStoredConfig = configManager.begin();
  configManager.load(cfg);

  Serial.println(F("PersistentConfigTest ready."));
  Serial.println(hadStoredConfig ? F("Loaded config from flash.") : F("No valid config found, defaults were written."));
  Serial.println(F("Commands: p=print, a=toggle light auto, f=toggle fan auto, d=restore defaults"));
  printConfig(cfg);
}

void loop() {
  handleSerialCommand();
}
