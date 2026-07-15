#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <ArduinoOTA.h>
#include <InternalStorage.h>
#include <PubSubClient.h>

#include "Credentials.h"

#ifndef WIFI_SSID
#error "Credentials.h must define WIFI_SSID."
#endif
#ifndef WIFI_PASSWORD
#error "Credentials.h must define WIFI_PASSWORD."
#endif
#ifndef MQTT_HOST
#error "Credentials.h must define MQTT_HOST."
#endif
#ifndef MQTT_PORT
#error "Credentials.h must define MQTT_PORT."
#endif
#ifndef MQTT_USERNAME
#error "Credentials.h must define MQTT_USERNAME."
#endif
#ifndef MQTT_PASSWORD
#error "Credentials.h must define MQTT_PASSWORD."
#endif
#ifndef OTA_NAME
#error "Credentials.h must define OTA_NAME."
#endif
#ifndef OTA_PASSWORD
#error "Credentials.h must define OTA_PASSWORD."
#endif

#ifndef SHT31_I2C_ADDRESS
#define SHT31_I2C_ADDRESS 0x44
#endif

namespace {

constexpr char TEST_ID[] = "i2c_passive_baseline";
constexpr char MQTT_STATUS_TOPIC[] = "smaeenhouse/test/i2c_passive_baseline/status";
constexpr char MQTT_EVENT_TOPIC[] = "smaeenhouse/test/i2c_passive_baseline/event";

constexpr uint8_t PIN_SHT_ALERT = 7;
constexpr uint8_t PIN_RTC_ALARM = 10;
constexpr uint8_t PIN_FAN_SWITCH = 2;
constexpr uint8_t PIN_FAN_TACH = A1;
constexpr uint8_t PIN_LIGHT_POWER_RELAY = 3;
constexpr uint8_t PIN_LIGHT_DIM_SHDN = 4;
constexpr uint8_t PIN_SOIL_MOISTURE = A0;
constexpr uint8_t PIN_LIGHT_SENSOR_INT = 9;

constexpr uint8_t FAN_OFF_LEVEL = LOW;
constexpr uint8_t LIGHT_RELAY_OPEN_LEVEL = LOW;
constexpr uint8_t LIGHT_DIM_SHDN_ASSERTED_LEVEL = LOW;

constexpr uint8_t I2C_ADDRESS_SHT31 = SHT31_I2C_ADDRESS;
constexpr uint8_t I2C_ADDRESS_DS3231 = 0x68;
constexpr uint8_t I2C_ADDRESS_AT24C32 = 0x57;
constexpr uint8_t I2C_ADDRESS_AD5263 = 0x2C;
constexpr uint8_t I2C_ADDRESS_TSL25911 = 0x29;

constexpr uint32_t NETWORK_OPERATION_TIMEOUT_MS = 1000UL;
constexpr uint32_t WIFI_SETTLE_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000UL;
constexpr uint32_t OTA_MAX_POLL_GAP_MS = 2000UL;
constexpr uint32_t I2C_PROBE_INTERVAL_MS = 30000UL;
constexpr uint32_t STATUS_PUBLISH_INTERVAL_MS = 30000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 30000UL;
constexpr uint16_t MQTT_PACKET_BUFFER_SIZE = 512;

volatile bool shtAlertPending = false;
volatile bool rtcAlarmPending = false;
volatile uint32_t fanTachPulseCount = 0;

void onShtAlert() {
  shtAlertPending = true;
}

void onRtcAlarm() {
  rtcAlarmPending = true;
}

void onFanTachPulse() {
  fanTachPulseCount++;
}

enum class WifiConnectState : uint8_t {
  Idle,
  Settling,
  Connecting
};

struct I2cInventoryState {
  bool sht31 = false;
  bool ds3231 = false;
  bool at24c32 = false;
  bool ad5263 = false;
  bool tsl25911 = false;
  uint32_t probeCount = 0;
  uint8_t lastError = 0;
};

WiFiClient networkClient;
PubSubClient mqttClient(networkClient);

WifiConnectState wifiConnectState = WifiConnectState::Idle;
I2cInventoryState i2cState;
bool wifiWasConnected = false;
bool mqttWasConnected = false;
bool otaInitialized = false;
uint32_t wifiStateStartedMs = 0;
uint32_t nextWifiAttemptMs = 0;
uint32_t nextMqttAttemptMs = 0;
uint32_t lastOtaPollMs = 0;
uint32_t otaPollGapViolations = 0;
uint32_t lastI2cProbeMs = 0;
uint32_t lastStatusPublishMs = 0;
uint32_t lastStatusPrintMs = 0;
uint32_t safeStateEnforceCount = 0;

bool serialAvailable() {
  return static_cast<bool>(Serial);
}

void enforceSafeOutputs() {
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);
  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  safeStateEnforceCount++;
}

void configurePinsForSafeState() {
  digitalWrite(PIN_LIGHT_POWER_RELAY, LIGHT_RELAY_OPEN_LEVEL);
  pinMode(PIN_LIGHT_POWER_RELAY, OUTPUT);

  digitalWrite(PIN_FAN_SWITCH, FAN_OFF_LEVEL);
  pinMode(PIN_FAN_SWITCH, OUTPUT);

  digitalWrite(PIN_LIGHT_DIM_SHDN, LIGHT_DIM_SHDN_ASSERTED_LEVEL);
  pinMode(PIN_LIGHT_DIM_SHDN, OUTPUT);

  pinMode(PIN_SHT_ALERT, INPUT_PULLUP);
  pinMode(PIN_RTC_ALARM, INPUT_PULLUP);
  pinMode(PIN_FAN_TACH, INPUT_PULLUP);
  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);
  pinMode(PIN_SOIL_MOISTURE, INPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_SHT_ALERT), onShtAlert, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_RTC_ALARM), onRtcAlarm, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_FAN_TACH), onFanTachPulse, FALLING);
}

bool probeI2cAddress(uint8_t address) {
  Wire.beginTransmission(address);
  const uint8_t result = Wire.endTransmission();
  i2cState.lastError = result;
  return result == 0;
}

void probeI2cInventory(uint32_t nowMs, bool force) {
  if (!force && (nowMs - lastI2cProbeMs) < I2C_PROBE_INTERVAL_MS) {
    return;
  }

  lastI2cProbeMs = nowMs;
  i2cState.probeCount++;
  i2cState.sht31 = probeI2cAddress(I2C_ADDRESS_SHT31);
  i2cState.ds3231 = probeI2cAddress(I2C_ADDRESS_DS3231);
  i2cState.at24c32 = probeI2cAddress(I2C_ADDRESS_AT24C32);
  i2cState.ad5263 = probeI2cAddress(I2C_ADDRESS_AD5263);
  i2cState.tsl25911 = probeI2cAddress(I2C_ADDRESS_TSL25911);

  if (serialAvailable()) {
    Serial.print(F("[I2C] Probe #"));
    Serial.print(i2cState.probeCount);
    Serial.print(F(" sht31="));
    Serial.print(i2cState.sht31 ? F("OK") : F("MISS"));
    Serial.print(F(" ds3231="));
    Serial.print(i2cState.ds3231 ? F("OK") : F("MISS"));
    Serial.print(F(" at24c32="));
    Serial.print(i2cState.at24c32 ? F("OK") : F("MISS"));
    Serial.print(F(" ad5263="));
    Serial.print(i2cState.ad5263 ? F("OK") : F("MISS"));
    Serial.print(F(" tsl25911="));
    Serial.print(i2cState.tsl25911 ? F("OK") : F("MISS"));
    Serial.print(F(" last_error="));
    Serial.println(i2cState.lastError);
  }
}

void printIpAddress(const __FlashStringHelper* label, const IPAddress& address) {
  if (!serialAvailable()) {
    return;
  }

  Serial.print(label);
  Serial.println(address);
}

void printWifiConnected() {
  if (!serialAvailable()) {
    return;
  }

  Serial.print(F("[WiFi] Connected to "));
  Serial.println(WiFi.SSID());
  printIpAddress(F("[WiFi] IP: "), WiFi.localIP());
  Serial.print(F("[WiFi] RSSI: "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}

void beginOta() {
  ArduinoOTA.begin(WiFi.localIP(), OTA_NAME, OTA_PASSWORD, InternalStorage);
  otaInitialized = true;
  lastOtaPollMs = millis();

  if (serialAvailable()) {
    Serial.print(F("[OTA] Enabled as "));
    Serial.println(OTA_NAME);
  }
}

void publishEvent(const char* eventName) {
  if (!mqttClient.connected()) {
    return;
  }

  char payload[192];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"event\":\"%s\"}",
           TEST_ID,
           static_cast<unsigned long>(millis() / 1000UL),
           eventName);
  const bool published = mqttClient.publish(MQTT_EVENT_TOPIC, payload, false);

  if (serialAvailable()) {
    Serial.print(F("[MQTT] Published event "));
    Serial.print(eventName);
    Serial.print(F("="));
    Serial.println(published ? F("YES") : F("NO"));
  }
}

void publishStatus(uint32_t nowMs, bool force) {
  if (!mqttClient.connected()) {
    return;
  }

  if (!force && (nowMs - lastStatusPublishMs) < STATUS_PUBLISH_INTERVAL_MS) {
    return;
  }

  lastStatusPublishMs = nowMs;

  noInterrupts();
  const bool shtPending = shtAlertPending;
  const bool rtcPending = rtcAlarmPending;
  const uint32_t tachPulses = fanTachPulseCount;
  interrupts();

  char payload[384];
  snprintf(payload,
           sizeof(payload),
           "{\"test\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"mqtt\":true,\"ota\":%s,\"ota_gap\":%lu,\"safe\":{\"fan\":\"off\",\"relay\":\"open\",\"shdn\":\"asserted\",\"count\":%lu},\"i2c\":{\"sht31\":%s,\"ds3231\":%s,\"at24c32\":%s,\"ad5263\":%s,\"tsl25911\":%s,\"probes\":%lu,\"last_error\":%u},\"isr\":{\"sht\":%s,\"rtc\":%s,\"tach\":%lu}}",
           TEST_ID,
           static_cast<unsigned long>(nowMs / 1000UL),
           WiFi.status() == WL_CONNECTED ? "true" : "false",
           otaInitialized ? "true" : "false",
           static_cast<unsigned long>(otaPollGapViolations),
           static_cast<unsigned long>(safeStateEnforceCount),
           i2cState.sht31 ? "true" : "false",
           i2cState.ds3231 ? "true" : "false",
           i2cState.at24c32 ? "true" : "false",
           i2cState.ad5263 ? "true" : "false",
           i2cState.tsl25911 ? "true" : "false",
           static_cast<unsigned long>(i2cState.probeCount),
           i2cState.lastError,
           shtPending ? "true" : "false",
           rtcPending ? "true" : "false",
           static_cast<unsigned long>(tachPulses));
  const bool published = mqttClient.publish(MQTT_STATUS_TOPIC, payload, true);

  if (serialAvailable()) {
    Serial.print(F("[MQTT] Published I2C status="));
    Serial.println(published ? F("YES") : F("NO"));
  }
}

void onWifiConnected() {
  printWifiConnected();
  beginOta();
  nextMqttAttemptMs = millis();
}

void onWifiDisconnected() {
  otaInitialized = false;

  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  mqttWasConnected = false;

  if (serialAvailable()) {
    Serial.println(F("[WiFi] Connection lost; reconnect scheduled."));
  }
}

void startWifiReconnect(uint32_t nowMs) {
  if (serialAvailable()) {
    Serial.print(F("[WiFi] Connecting to "));
    Serial.println(WIFI_SSID);
  }

  WiFi.disconnect();
  wifiConnectState = WifiConnectState::Settling;
  wifiStateStartedMs = nowMs;
}

void serviceWifi(uint32_t nowMs) {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;

  if (wifiConnected) {
    wifiConnectState = WifiConnectState::Idle;
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      onWifiConnected();
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    onWifiDisconnected();
    wifiConnectState = WifiConnectState::Idle;
    nextWifiAttemptMs = nowMs;
  }

  switch (wifiConnectState) {
    case WifiConnectState::Idle:
      if (static_cast<int32_t>(nowMs - nextWifiAttemptMs) >= 0) {
        startWifiReconnect(nowMs);
      }
      break;

    case WifiConnectState::Settling:
      if ((nowMs - wifiStateStartedMs) >= WIFI_SETTLE_MS) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        wifiConnectState = WifiConnectState::Connecting;
        wifiStateStartedMs = millis();
      }
      break;

    case WifiConnectState::Connecting:
      if ((nowMs - wifiStateStartedMs) >= WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect();
        wifiConnectState = WifiConnectState::Idle;
        nextWifiAttemptMs = nowMs + WIFI_RETRY_INTERVAL_MS;

        if (serialAvailable()) {
          Serial.println(F("[WiFi] Connect timeout; retry scheduled."));
        }
      }
      break;
  }
}

void serviceOta() {
  if (!otaInitialized || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const uint32_t beforePollMs = millis();
  const uint32_t pollGapMs = beforePollMs - lastOtaPollMs;
  if (pollGapMs > OTA_MAX_POLL_GAP_MS) {
    otaPollGapViolations++;
    if (serialAvailable()) {
      Serial.print(F("[OTA] WARNING: poll gap was "));
      Serial.print(pollGapMs);
      Serial.println(F(" ms."));
    }
  }

  ArduinoOTA.poll();
  lastOtaPollMs = millis();
}

void serviceMqtt(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    mqttWasConnected = false;
    return;
  }

  if (!mqttClient.connected()) {
    if (mqttWasConnected) {
      mqttWasConnected = false;
      if (serialAvailable()) {
        Serial.println(F("[MQTT] Disconnected."));
      }
    }

    if (static_cast<int32_t>(nowMs - nextMqttAttemptMs) < 0) {
      return;
    }

    nextMqttAttemptMs = nowMs + MQTT_RECONNECT_INTERVAL_MS;

    if (serialAvailable()) {
      Serial.print(F("[MQTT] Connecting to "));
      Serial.print(MQTT_HOST);
      Serial.print(':');
      Serial.println(MQTT_PORT);
    }

    const bool connected = mqttClient.connect(TEST_ID, MQTT_USERNAME, MQTT_PASSWORD);
    if (!connected) {
      if (serialAvailable()) {
        Serial.print(F("[MQTT] Connect failed, state="));
        Serial.println(mqttClient.state());
      }
      return;
    }

    mqttWasConnected = true;
    if (serialAvailable()) {
      Serial.println(F("[MQTT] Connected."));
    }
    publishEvent("mqtt_connected");
    publishStatus(nowMs, true);
  }

  mqttClient.loop();
  publishStatus(nowMs, false);
}

void printStatus(uint32_t nowMs) {
  if (!serialAvailable() || (nowMs - lastStatusPrintMs) < STATUS_PRINT_INTERVAL_MS) {
    return;
  }

  lastStatusPrintMs = nowMs;

  Serial.print(F("[Status] uptime="));
  Serial.print(nowMs / 1000UL);
  Serial.print(F(" s, wifi="));
  Serial.print(WiFi.status() == WL_CONNECTED ? F("UP") : F("DOWN"));
  Serial.print(F(", mqtt="));
  Serial.print(mqttClient.connected() ? F("UP") : F("DOWN"));
  Serial.print(F(", ota="));
  Serial.print(otaInitialized ? F("READY") : F("WAITING_FOR_WIFI"));
  Serial.print(F(", i2c_probes="));
  Serial.print(i2cState.probeCount);
  Serial.print(F(", devices="));
  Serial.print(i2cState.sht31 ? F("SHT31 ") : F(""));
  Serial.print(i2cState.ds3231 ? F("DS3231 ") : F(""));
  Serial.print(i2cState.at24c32 ? F("AT24C32 ") : F(""));
  Serial.print(i2cState.ad5263 ? F("AD5263 ") : F(""));
  Serial.print(i2cState.tsl25911 ? F("TSL25911") : F(""));
  Serial.print(F(", ota_gap_violations="));
  Serial.println(otaPollGapViolations);
}

}  // namespace

void setup() {
  configurePinsForSafeState();
  enforceSafeOutputs();

  Serial.begin(115200);
  // Native USB Serial is optional. Never wait for a host to open the port.

  Wire.begin();
  probeI2cInventory(millis(), true);

  WiFi.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  networkClient.setTimeout(NETWORK_OPERATION_TIMEOUT_MS);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setSocketTimeout(NETWORK_OPERATION_TIMEOUT_MS / 1000UL);
  mqttClient.setBufferSize(MQTT_PACKET_BUFFER_SIZE);

  nextWifiAttemptMs = millis();

  if (serialAvailable()) {
    Serial.println();
    Serial.println(F("Grow Controller I2C Passive Baseline Test"));
    Serial.println(F("Runtime order: safe outputs -> I2C known-address probe -> WiFi -> OTA -> direct MQTT test topics"));
  }
}

void loop() {
  serviceOta();

  uint32_t nowMs = millis();
  probeI2cInventory(nowMs, false);
  serviceWifi(nowMs);
  serviceOta();

  nowMs = millis();
  serviceMqtt(nowMs);
  serviceOta();

  printStatus(millis());
}
