#include <Arduino.h>
#include <ArduinoHA.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <math.h>

// Edit these test-only values locally before uploading to real hardware.
// This sketch deliberately does not include production Credentials.h.
constexpr char TEST_WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char TEST_WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

constexpr char TEST_MQTT_HOST[] = "MQTT.local";
constexpr uint16_t TEST_MQTT_PORT = 1883;
constexpr char TEST_MQTT_USERNAME[] = "mqtt_user";
constexpr char TEST_MQTT_PASSWORD[] = "mqtt_password";
constexpr char TEST_MQTT_PREFIX[] = "homeassistant";

constexpr char TEST_DEVICE_NAME[] = "Grow Controller Network Diagnostics";
constexpr char TEST_DEVICE_ID[] = "grow_controller_network_diag";

constexpr char TEST_NTP_SERVER[] = "pool.ntp.org";
constexpr char TEST_NTP_SERVER_2[] = "time.google.com";
constexpr char TEST_NTP_SERVER_3[] = "time.cloudflare.com";
constexpr int32_t TEST_UTC_OFFSET_SECONDS = 3600;
constexpr int32_t TEST_DST_OFFSET_SECONDS = 3600;

namespace {

constexpr uint16_t NTP_PORT = 123;
constexpr uint16_t NTP_LOCAL_PORT = 2390;
constexpr uint16_t NTP_PACKET_SIZE = 48;
constexpr uint32_t NTP_EPOCH_OFFSET = 2208988800UL;
constexpr uint32_t NTP_RESPONSE_TIMEOUT_MS = 3000UL;
constexpr uint32_t NTP_INTER_ATTEMPT_DELAY_MS = 250UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
constexpr uint32_t WIFI_DISCONNECT_SETTLE_MS = 1500UL;
constexpr uint8_t WIFI_CONNECT_ATTEMPTS = 2;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 10000UL;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 10000UL;

struct SimulatedState {
  float temperature = 23.5f;
  float humidity = 54.0f;
  int32_t soilRaw = 520;
  int32_t soilPercent = 45;
  float cabinetLux = 128.5f;
  int32_t cabinetFullSpectrum = 730;
  int32_t cabinetInfrared = 210;
  int32_t cabinetVisible = 520;
  int32_t fanRpm = 1210;
  const char* lightFaultReason = "none";

  bool lightFault = false;
  bool fanFault = false;
  bool shtFault = false;
  bool rtcFault = false;
  bool eepromFault = false;
  bool lightSensorFault = false;

  bool fanManualState = false;
  bool fanAutoMode = true;
  bool lightAutoMode = true;
  bool lightHardPowerOff = false;
  bool lightFallbackToAuto = true;
  bool growLightOn = true;
  uint8_t growLightBrightness = 65;

  float tempHighSet = 30.0f;
  float tempHighClear = 28.0f;
  float tempLowSet = 16.0f;
  float tempLowClear = 18.0f;
  float humHighSet = 80.0f;
  float humHighClear = 75.0f;
  float humLowSet = 35.0f;
  float humLowClear = 40.0f;

  uint16_t lightOnTimeMinutes = 480;
  uint16_t lightOffTimeMinutes = 1200;
  uint16_t defaultLightDimMinutes = 30;

  int16_t soilAir = 820;
  int16_t soilWater = 300;
  int16_t soilDepthMm = 80;

  uint8_t haDimTargetPercent = 100;
  uint16_t haDimDurationMinutes = 30;
};

SimulatedState state;

WiFiClient networkClient;
HADevice device(TEST_DEVICE_ID);
HAMqtt mqtt(networkClient, device, 42);

HASensorNumber temperatureSensor("temperature");
HASensorNumber humiditySensor("humidity");
HASensorNumber soilPercentSensor("soil_moisture_percent");
HASensorNumber soilRawSensor("soil_moisture_raw");
HASensorNumber cabinetIlluminanceSensor("cabinet_illuminance_lux");
HASensorNumber cabinetFullSpectrumSensor("cabinet_light_full_spectrum_raw");
HASensorNumber cabinetInfraredSensor("cabinet_light_infrared_raw");
HASensorNumber cabinetVisibleSensor("cabinet_light_visible_raw");
HASensorNumber fanRpmSensor("fan_rpm");
HASensor lightFaultReasonSensor("light_fault_reason");

HABinarySensor lightFaultBinarySensor("light_fault");
HABinarySensor fanFaultBinarySensor("fan_fault");
HABinarySensor shtFaultBinarySensor("sht_fault");
HABinarySensor rtcFaultBinarySensor("rtc_fault");
HABinarySensor eepromFaultBinarySensor("eeprom_fault");
HABinarySensor lightSensorFaultBinarySensor("light_sensor_fault");

HASwitch fanSwitch("fan");
HASwitch fanAutoModeSwitch("fan_auto_mode");
HASwitch lightAutoModeSwitch("light_auto_mode");
HASwitch lightHardPowerOffSwitch("light_hard_power_off");
HASwitch lightFallbackUseAutoModeSwitch("light_fallback_to_auto");

HALight growLight("grow_light", HALight::BrightnessFeature);

HANumber tempHighSetNumber("temp_high_set", HANumber::PrecisionP1);
HANumber tempHighClearNumber("temp_high_clear", HANumber::PrecisionP1);
HANumber tempLowSetNumber("temp_low_set", HANumber::PrecisionP1);
HANumber tempLowClearNumber("temp_low_clear", HANumber::PrecisionP1);
HANumber humHighSetNumber("hum_high_set", HANumber::PrecisionP1);
HANumber humHighClearNumber("hum_high_clear", HANumber::PrecisionP1);
HANumber humLowSetNumber("hum_low_set", HANumber::PrecisionP1);
HANumber humLowClearNumber("hum_low_clear", HANumber::PrecisionP1);
HANumber lightOnTimeNumber("light_on_time_minutes");
HANumber lightOffTimeNumber("light_off_time_minutes");
HANumber defaultLightDimMinutesNumber("light_dim_minutes");
HANumber soilAirNumber("soil_air");
HANumber soilWaterNumber("soil_water");
HANumber soilDepthNumber("soil_depth_mm");
HANumber haDimTargetPercentNumber("ha_dim_target_percent");
HANumber haDimDurationMinutesNumber("ha_dim_duration_minutes");

HAButton syncTimeButton("sync_time");
HAButton readSoilRawButton("read_soil_raw_value");
HAButton startHaDimButton("start_ha_dim");

bool diagnosticAvailable = true;
bool wasMqttConnected = false;
uint32_t lastMqttReconnectAttemptMs = 0;
uint32_t lastStatusPrintMs = 0;

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

uint16_t clampUInt16(long value, uint16_t minValue, uint16_t maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return static_cast<uint16_t>(value);
}

uint8_t clampPercent(long value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return static_cast<uint8_t>(value);
}

void printIpAddress(const __FlashStringHelper* label, const IPAddress& address) {
  Serial.print(label);
  Serial.println(address);
}

void printTwoDigits(uint8_t value) {
  if (value < 10) {
    Serial.print('0');
  }
  Serial.print(value);
}

bool isLeapYear(uint16_t year) {
  return ((year % 4U) == 0U && (year % 100U) != 0U) || ((year % 400U) == 0U);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return daysPerMonth[month - 1];
}

void printUnixDateTime(uint32_t epoch) {
  uint32_t days = epoch / 86400UL;
  uint32_t secondsOfDay = epoch % 86400UL;

  uint16_t year = 1970;
  while (true) {
    const uint16_t daysThisYear = isLeapYear(year) ? 366U : 365U;
    if (days < daysThisYear) {
      break;
    }
    days -= daysThisYear;
    year++;
  }

  uint8_t month = 1;
  while (true) {
    const uint8_t daysThisMonth = daysInMonth(year, month);
    if (days < daysThisMonth) {
      break;
    }
    days -= daysThisMonth;
    month++;
  }

  const uint8_t day = static_cast<uint8_t>(days + 1);
  const uint8_t hour = static_cast<uint8_t>(secondsOfDay / 3600UL);
  secondsOfDay %= 3600UL;
  const uint8_t minute = static_cast<uint8_t>(secondsOfDay / 60UL);
  const uint8_t second = static_cast<uint8_t>(secondsOfDay % 60UL);

  Serial.print(year);
  Serial.print('-');
  printTwoDigits(month);
  Serial.print('-');
  printTwoDigits(day);
  Serial.print(' ');
  printTwoDigits(hour);
  Serial.print(':');
  printTwoDigits(minute);
  Serial.print(':');
  printTwoDigits(second);
}

void printWifiStatus() {
  Serial.print(F("WiFi.status="));
  Serial.println(WiFi.status());

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  Serial.print(F("Connected SSID="));
  Serial.println(WiFi.SSID());
  printIpAddress(F("Local IP="), WiFi.localIP());
  printIpAddress(F("Gateway="), WiFi.gatewayIP());
  printIpAddress(F("Subnet="), WiFi.subnetMask());
  printIpAddress(F("DNS="), WiFi.dnsIP());
  Serial.print(F("RSSI="));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
}

bool ensureWifiConnected() {
  Serial.println(F("[WiFi] Begin connection test."));
  Serial.print(F("[WiFi] Firmware version="));
  Serial.println(WiFi.firmwareVersion());
  Serial.print(F("[WiFi] Target SSID="));
  Serial.println(TEST_WIFI_SSID);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("[WiFi] Already connected."));
    printWifiStatus();
    return true;
  }

  Serial.println(F("[WiFi] Forcing disconnect before reconnect attempts."));
  WiFi.disconnect();
  delay(WIFI_DISCONNECT_SETTLE_MS);

  for (uint8_t attempt = 1; attempt <= WIFI_CONNECT_ATTEMPTS; attempt++) {
    Serial.print(F("[WiFi] Connect attempt "));
    Serial.print(attempt);
    Serial.print(F(" of "));
    Serial.println(WIFI_CONNECT_ATTEMPTS);

    WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASSWORD);
    const uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < WIFI_CONNECT_TIMEOUT_MS) {
      Serial.print('.');
      delay(500);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("[WiFi] Connect result=OK"));
      printWifiStatus();
      return true;
    }

    Serial.print(F("[WiFi] Connect attempt failed, status="));
    Serial.println(WiFi.status());
    if (attempt < WIFI_CONNECT_ATTEMPTS) {
      Serial.println(F("[WiFi] Disconnecting and waiting before retry."));
      WiFi.disconnect();
      delay(WIFI_DISCONNECT_SETTLE_MS);
    }
  }

  Serial.println(F("[WiFi] Connect result=FAILED"));
  printWifiStatus();
  return false;
}

bool resolveHost(const char* label, const char* host, IPAddress& ip) {
  Serial.print(F("[DNS] Resolving "));
  Serial.print(label);
  Serial.print(F(" host "));
  Serial.println(host);

  const int result = WiFi.hostByName(host, ip);
  Serial.print(F("[DNS] Result code="));
  Serial.println(result);
  if (result == 1) {
    Serial.print(F("[DNS] Address="));
    Serial.println(ip);
    return true;
  }

  Serial.println(F("[DNS] FAILED"));
  return false;
}

bool testMqttTcpConnection() {
  IPAddress mqttIp;
  if (!resolveHost("MQTT", TEST_MQTT_HOST, mqttIp)) {
    Serial.println(F("[MQTT TCP] Skipped because DNS failed."));
    return false;
  }

  WiFiClient tcpClient;
  Serial.print(F("[MQTT TCP] Connecting to "));
  Serial.print(TEST_MQTT_HOST);
  Serial.print(':');
  Serial.println(TEST_MQTT_PORT);

  const bool connected = tcpClient.connect(TEST_MQTT_HOST, TEST_MQTT_PORT);
  Serial.print(F("[MQTT TCP] Result="));
  Serial.println(connected ? F("OK") : F("FAILED"));
  tcpClient.stop();
  return connected;
}

struct NtpRequestVariant {
  const __FlashStringHelper* name;
  uint8_t firstByte;
  bool useLegacyHeaderFields;
};

uint32_t readNtpTimestampSeconds(const uint8_t* packetBuffer, uint8_t offset) {
  return (static_cast<uint32_t>(packetBuffer[offset]) << 24) |
         (static_cast<uint32_t>(packetBuffer[offset + 1]) << 16) |
         (static_cast<uint32_t>(packetBuffer[offset + 2]) << 8) |
         static_cast<uint32_t>(packetBuffer[offset + 3]);
}

void printHexByte(uint8_t value) {
  if (value < 16) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void printNtpPacketHexDump(const uint8_t* packetBuffer, uint8_t length) {
  Serial.print(F("[NTP] First "));
  Serial.print(length);
  Serial.println(F(" response bytes:"));
  for (uint8_t i = 0; i < length; i++) {
    if ((i % 16) == 0) {
      Serial.print(F("[NTP]   "));
    }
    printHexByte(packetBuffer[i]);
    Serial.print(' ');
    if ((i % 16) == 15 || i == (length - 1)) {
      Serial.println();
    }
  }
}

void printNtpHeaderBits(const __FlashStringHelper* label, uint8_t firstByte) {
  Serial.print(label);
  Serial.print(F(" LI="));
  Serial.print((firstByte >> 6) & 0x03);
  Serial.print(F(", VN="));
  Serial.print((firstByte >> 3) & 0x07);
  Serial.print(F(", Mode="));
  Serial.println(firstByte & 0x07);
}

void printNtpResponseDetails(const uint8_t* packetBuffer) {
  printNtpHeaderBits(F("[NTP] Response header"), packetBuffer[0]);
  Serial.print(F("[NTP] Stratum="));
  Serial.print(packetBuffer[1]);
  Serial.print(F(", Poll="));
  Serial.print(static_cast<int8_t>(packetBuffer[2]));
  Serial.print(F(", Precision="));
  Serial.println(static_cast<int8_t>(packetBuffer[3]));

  Serial.print(F("[NTP] Root delay raw=0x"));
  printHexByte(packetBuffer[4]);
  printHexByte(packetBuffer[5]);
  printHexByte(packetBuffer[6]);
  printHexByte(packetBuffer[7]);
  Serial.print(F(", root dispersion raw=0x"));
  printHexByte(packetBuffer[8]);
  printHexByte(packetBuffer[9]);
  printHexByte(packetBuffer[10]);
  printHexByte(packetBuffer[11]);
  Serial.println();

  const uint32_t originateSeconds = readNtpTimestampSeconds(packetBuffer, 24);
  const uint32_t receiveSeconds = readNtpTimestampSeconds(packetBuffer, 32);
  const uint32_t transmitSeconds = readNtpTimestampSeconds(packetBuffer, 40);

  Serial.print(F("[NTP] Originate timestamp seconds="));
  Serial.println(originateSeconds);
  Serial.print(F("[NTP] Receive timestamp seconds="));
  Serial.println(receiveSeconds);
  Serial.print(F("[NTP] Transmit timestamp seconds="));
  Serial.println(transmitSeconds);
}

void printNtpUnixTime(uint32_t secondsSince1900) {
  Serial.print(F("[NTP] Raw transmit timestamp seconds="));
  Serial.println(secondsSince1900);

  if (secondsSince1900 <= NTP_EPOCH_OFFSET) {
    Serial.println(F("[NTP] FAILED: timestamp is older than Unix epoch offset."));
    return;
  }

  const uint32_t unixUtc = secondsSince1900 - NTP_EPOCH_OFFSET;
  Serial.print(F("[NTP] UTC epoch="));
  Serial.println(unixUtc);
  Serial.print(F("[NTP] UTC time="));
  printUnixDateTime(unixUtc);
  Serial.println();

  const int64_t localEpochSigned = static_cast<int64_t>(unixUtc) +
                                   static_cast<int64_t>(TEST_UTC_OFFSET_SECONDS) +
                                   static_cast<int64_t>(TEST_DST_OFFSET_SECONDS);
  Serial.print(F("[NTP] Local epoch="));
  Serial.println(static_cast<long>(localEpochSigned));
  if (localEpochSigned > 0 && localEpochSigned <= 0xFFFFFFFFLL) {
    Serial.print(F("[NTP] Local time="));
    printUnixDateTime(static_cast<uint32_t>(localEpochSigned));
    Serial.println();
  } else {
    Serial.println(F("[NTP] Local time unavailable: offset produced invalid epoch."));
  }
}

bool printWifiNinaModuleTime() {
  Serial.println(F("[NTP] Checking WiFiNINA module time via WiFi.getTime()."));
  const uint32_t moduleEpoch = WiFi.getTime();
  Serial.print(F("[NTP] WiFi.getTime epoch="));
  Serial.println(moduleEpoch);

  if (moduleEpoch == 0) {
    Serial.println(F("[NTP] WiFi.getTime result=FAILED_OR_NOT_SYNCED"));
    return false;
  }

  Serial.print(F("[NTP] WiFi.getTime UTC time="));
  printUnixDateTime(moduleEpoch);
  Serial.println();
  Serial.println(F("[NTP] WiFi.getTime result=OK"));
  return true;
}

bool runSingleNtpAttempt(const char* serverName, const NtpRequestVariant& variant, uint16_t localPort) {
  Serial.println(F("[NTP] --------------------------------------------------"));
  Serial.print(F("[NTP] Server="));
  Serial.println(serverName);
  Serial.print(F("[NTP] Request variant="));
  Serial.println(variant.name);
  printNtpHeaderBits(F("[NTP] Request header"), variant.firstByte);

  WiFiUDP udp;
  Serial.print(F("[NTP] Opening local UDP port "));
  Serial.println(localPort);
  if (!udp.begin(localPort)) {
    Serial.println(F("[NTP] FAILED: udp.begin() returned false."));
    return false;
  }
  Serial.println(F("[NTP] UDP begin OK."));

  IPAddress ntpIp;
  if (!resolveHost("NTP", serverName, ntpIp)) {
    udp.stop();
    Serial.println(F("[NTP] FAILED: DNS resolution failed."));
    return false;
  }

  uint8_t packetBuffer[NTP_PACKET_SIZE] = {0};
  packetBuffer[0] = variant.firstByte;
  if (variant.useLegacyHeaderFields) {
    packetBuffer[1] = 0;
    packetBuffer[2] = 6;
    packetBuffer[3] = 0xEC;
  }

  Serial.print(F("[NTP] beginPacket "));
  Serial.print(ntpIp);
  Serial.print(':');
  Serial.println(NTP_PORT);
  if (!udp.beginPacket(ntpIp, NTP_PORT)) {
    udp.stop();
    Serial.println(F("[NTP] FAILED: beginPacket() returned false."));
    return false;
  }

  const size_t bytesWritten = udp.write(packetBuffer, NTP_PACKET_SIZE);
  Serial.print(F("[NTP] Bytes written="));
  Serial.println(bytesWritten);
  if (bytesWritten != NTP_PACKET_SIZE) {
    udp.stop();
    Serial.println(F("[NTP] FAILED: short UDP write."));
    return false;
  }

  if (!udp.endPacket()) {
    udp.stop();
    Serial.println(F("[NTP] FAILED: endPacket() returned false."));
    return false;
  }
  Serial.println(F("[NTP] Packet sent."));

  bool sawAnyPacket = false;
  const uint32_t startMs = millis();
  while ((millis() - startMs) < NTP_RESPONSE_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      sawAnyPacket = true;
      Serial.print(F("[NTP] Response packet size="));
      Serial.println(packetSize);
      Serial.print(F("[NTP] Response remote IP="));
      Serial.println(udp.remoteIP());
      Serial.print(F("[NTP] Response remote port="));
      Serial.println(udp.remotePort());
    }

    if (packetSize >= static_cast<int>(NTP_PACKET_SIZE)) {
      udp.read(packetBuffer, NTP_PACKET_SIZE);
      udp.stop();

      printNtpPacketHexDump(packetBuffer, NTP_PACKET_SIZE);
      printNtpResponseDetails(packetBuffer);
      printNtpUnixTime(readNtpTimestampSeconds(packetBuffer, 40));

      const uint8_t responseMode = packetBuffer[0] & 0x07;
      const uint8_t stratum = packetBuffer[1];
      if (responseMode != 4) {
        Serial.println(F("[NTP] WARNING: response mode is not server mode 4."));
      }
      if (stratum == 0 || stratum > 15) {
        Serial.println(F("[NTP] WARNING: response stratum is outside normal 1..15 range."));
      }

      Serial.println(F("[NTP] Result=OK"));
      return true;
    }

    if (packetSize > 0) {
      Serial.println(F("[NTP] Ignoring short response packet."));
    }

    delay(10);
  }

  udp.stop();
  if (sawAnyPacket) {
    Serial.println(F("[NTP] FAILED: only short/incomplete packets arrived before timeout."));
  } else {
    Serial.println(F("[NTP] FAILED: no UDP response packet arrived before timeout."));
  }
  return false;
}

bool runNtpDiagnostics() {
  Serial.println(F("[NTP] Begin extended NTP diagnostics."));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[NTP] FAILED: WiFi is not connected."));
    return false;
  }

  const bool moduleTimeAvailableBeforeUdp = printWifiNinaModuleTime();

  const char* const servers[] = {
      TEST_NTP_SERVER,
      TEST_NTP_SERVER_2,
      TEST_NTP_SERVER_3,
  };

  const NtpRequestVariant variants[] = {
      {F("standard client request, LI=0 VN=4 Mode=3, otherwise zeroed"), 0x23, false},
      {F("legacy Arduino-style request, LI=3 VN=4 Mode=3"), 0xE3, true},
  };

  bool anySuccess = false;
  uint16_t localPort = NTP_LOCAL_PORT;

  for (uint8_t serverIndex = 0; serverIndex < (sizeof(servers) / sizeof(servers[0])); serverIndex++) {
    if (servers[serverIndex][0] == '\0') {
      continue;
    }

    for (uint8_t variantIndex = 0; variantIndex < (sizeof(variants) / sizeof(variants[0])); variantIndex++) {
      if (runSingleNtpAttempt(servers[serverIndex], variants[variantIndex], localPort)) {
        anySuccess = true;
      }
      localPort++;
      delay(NTP_INTER_ATTEMPT_DELAY_MS);
    }
  }

  Serial.println(F("[NTP] --------------------------------------------------"));
  const bool moduleTimeAvailableAfterUdp = printWifiNinaModuleTime();
  Serial.print(F("[NTP] Extended diagnostics result="));
  Serial.println(anySuccess || moduleTimeAvailableBeforeUdp || moduleTimeAvailableAfterUdp
                     ? F("AT LEAST ONE TIME SOURCE SUCCEEDED")
                     : F("ALL TIME SOURCES FAILED"));
  return anySuccess || moduleTimeAvailableBeforeUdp || moduleTimeAvailableAfterUdp;
}

void publishSwitchAndLightStates() {
  fanSwitch.setState(state.fanManualState);
  fanAutoModeSwitch.setState(state.fanAutoMode);
  lightAutoModeSwitch.setState(state.lightAutoMode);
  lightHardPowerOffSwitch.setState(state.lightHardPowerOff);
  lightFallbackUseAutoModeSwitch.setState(state.lightFallbackToAuto);
  growLight.setState(state.growLightOn);
  growLight.setBrightness(state.growLightBrightness);
}

void publishFaultStates(bool force) {
  lightFaultBinarySensor.setState(state.lightFault, force);
  fanFaultBinarySensor.setState(state.fanFault, force);
  shtFaultBinarySensor.setState(state.shtFault, force);
  rtcFaultBinarySensor.setState(state.rtcFault, force);
  eepromFaultBinarySensor.setState(state.eepromFault, force);
  lightSensorFaultBinarySensor.setState(state.lightSensorFault, force);
  lightFaultReasonSensor.setValue(state.lightFaultReason);
}

void publishConfigValues() {
  tempHighSetNumber.setState(state.tempHighSet);
  tempHighClearNumber.setState(state.tempHighClear);
  tempLowSetNumber.setState(state.tempLowSet);
  tempLowClearNumber.setState(state.tempLowClear);
  humHighSetNumber.setState(state.humHighSet);
  humHighClearNumber.setState(state.humHighClear);
  humLowSetNumber.setState(state.humLowSet);
  humLowClearNumber.setState(state.humLowClear);
  lightOnTimeNumber.setState(static_cast<int32_t>(state.lightOnTimeMinutes));
  lightOffTimeNumber.setState(static_cast<int32_t>(state.lightOffTimeMinutes));
  defaultLightDimMinutesNumber.setState(static_cast<int32_t>(state.defaultLightDimMinutes));
  soilAirNumber.setState(static_cast<int32_t>(state.soilAir));
  soilWaterNumber.setState(static_cast<int32_t>(state.soilWater));
  soilDepthNumber.setState(static_cast<int32_t>(state.soilDepthMm));
  haDimTargetPercentNumber.setState(static_cast<int32_t>(state.haDimTargetPercent));
  haDimDurationMinutesNumber.setState(static_cast<int32_t>(state.haDimDurationMinutes));
}

void publishSensorValues() {
  temperatureSensor.setValue(state.temperature);
  humiditySensor.setValue(state.humidity);
  soilRawSensor.setValue(state.soilRaw);
  soilPercentSensor.setValue(state.soilPercent);
  cabinetIlluminanceSensor.setValue(state.cabinetLux);
  cabinetFullSpectrumSensor.setValue(state.cabinetFullSpectrum);
  cabinetInfraredSensor.setValue(state.cabinetInfrared);
  cabinetVisibleSensor.setValue(state.cabinetVisible);
  fanRpmSensor.setValue(state.fanRpm);
}

void publishAllStates() {
  if (!mqtt.isConnected()) {
    Serial.println(F("[HA] Publish skipped: MQTT is not connected."));
    return;
  }

  publishSwitchAndLightStates();
  publishFaultStates(true);
  publishConfigValues();
  publishSensorValues();
  Serial.println(F("[HA] Published all simulated states."));
}

void rotateSyntheticValues() {
  state.temperature += 0.4f;
  if (state.temperature > 28.0f) {
    state.temperature = 21.0f;
  }

  state.humidity += 1.5f;
  if (state.humidity > 70.0f) {
    state.humidity = 45.0f;
  }

  state.soilRaw += 17;
  if (state.soilRaw > 760) {
    state.soilRaw = 420;
  }

  state.soilPercent += 4;
  if (state.soilPercent > 90) {
    state.soilPercent = 25;
  }

  state.cabinetLux += 30.0f;
  if (state.cabinetLux > 500.0f) {
    state.cabinetLux = 90.0f;
  }

  state.cabinetFullSpectrum += 21;
  state.cabinetInfrared += 9;
  state.cabinetVisible += 13;
  state.fanRpm += 95;
  if (state.fanRpm > 1800) {
    state.fanRpm = 900;
  }

  state.lightFault = !state.lightFault;
  state.lightFaultReason = state.lightFault ? "diagnostic simulated fault" : "none";
}

void printBriefStatus() {
  Serial.print(F("[Status] wifi="));
  Serial.print(WiFi.status() == WL_CONNECTED ? F("UP") : F("DOWN"));
  Serial.print(F(", mqtt="));
  Serial.print(mqtt.isConnected() ? F("UP") : F("DOWN"));
  Serial.print(F(", mqttState="));
  Serial.print(static_cast<int>(mqtt.getState()));
  Serial.print(F(", availability="));
  Serial.print(diagnosticAvailable ? F("ON") : F("OFF"));
  Serial.print(F(", temp="));
  Serial.print(state.temperature, 1);
  Serial.print(F(" C, humidity="));
  Serial.print(state.humidity, 1);
  Serial.println(F(" %"));
}

bool beginMqtt() {
  Serial.println(F("[MQTT] Starting ArduinoHA MQTT connection."));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[MQTT] Skipped: WiFi is not connected."));
    return false;
  }

  const bool beginOk = mqtt.begin(TEST_MQTT_HOST,
                                  TEST_MQTT_PORT,
                                  TEST_MQTT_USERNAME,
                                  TEST_MQTT_PASSWORD);
  Serial.print(F("[MQTT] begin() returned="));
  Serial.println(beginOk ? F("true") : F("false"));
  mqtt.loop();
  Serial.print(F("[MQTT] isConnected="));
  Serial.print(mqtt.isConnected() ? F("YES") : F("NO"));
  Serial.print(F(", state="));
  Serial.println(static_cast<int>(mqtt.getState()));

  device.setAvailability(mqtt.isConnected() && diagnosticAvailable);
  wasMqttConnected = mqtt.isConnected();
  if (wasMqttConnected) {
    publishAllStates();
  }

  return beginOk;
}

void runNetworkDiagnostics() {
  if (!ensureWifiConnected()) {
    Serial.println(F("[Diagnostics] WiFi failed; DNS, TCP, MQTT, and NTP checks need WiFi."));
    return;
  }

  testMqttTcpConnection();

  IPAddress ntpIp;
  resolveHost("NTP", TEST_NTP_SERVER, ntpIp);
}

void printHelp() {
  Serial.println(F("NetworkDiagnosticsTest commands:"));
  Serial.println(F("  h = print help"));
  Serial.println(F("  w = rerun WiFi, DNS, and MQTT TCP diagnostics"));
  Serial.println(F("  n = rerun detailed NTP diagnostics"));
  Serial.println(F("  m = reconnect MQTT"));
  Serial.println(F("  p = publish all simulated HA states"));
  Serial.println(F("  r = rotate synthetic sensor values and publish"));
  Serial.println(F("  a = toggle diagnostic availability"));
}

void onFanSwitchCommand(bool value, HASwitch* /*sender*/) {
  state.fanManualState = value;
  Serial.print(F("[HA command] fan="));
  Serial.println(value ? F("ON") : F("OFF"));
  publishSwitchAndLightStates();
}

void onFanAutoModeCommand(bool value, HASwitch* /*sender*/) {
  state.fanAutoMode = value;
  Serial.print(F("[HA command] fan_auto_mode="));
  Serial.println(value ? F("ON") : F("OFF"));
  publishSwitchAndLightStates();
}

void onLightAutoModeCommand(bool value, HASwitch* /*sender*/) {
  state.lightAutoMode = value;
  Serial.print(F("[HA command] light_auto_mode="));
  Serial.println(value ? F("ON") : F("OFF"));
  publishSwitchAndLightStates();
}

void onLightHardPowerOffCommand(bool value, HASwitch* /*sender*/) {
  state.lightHardPowerOff = value;
  Serial.print(F("[HA command] light_hard_power_off="));
  Serial.println(value ? F("ON") : F("OFF"));
  publishSwitchAndLightStates();
}

void onLightFallbackCommand(bool value, HASwitch* /*sender*/) {
  state.lightFallbackToAuto = value;
  Serial.print(F("[HA command] light_fallback_to_auto="));
  Serial.println(value ? F("ON") : F("OFF"));
  publishSwitchAndLightStates();
}

void onLightStateCommand(bool value, HALight* /*sender*/) {
  state.growLightOn = value;
  Serial.print(F("[HA command] grow_light state="));
  Serial.println(value ? F("ON") : F("OFF"));
  publishSwitchAndLightStates();
}

void onLightBrightnessCommand(uint8_t brightness, HALight* /*sender*/) {
  state.growLightBrightness = clampPercent(brightness);
  Serial.print(F("[HA command] grow_light brightness="));
  Serial.println(state.growLightBrightness);
  publishSwitchAndLightStates();
}

void printNumberCommand(const __FlashStringHelper* name, float value) {
  Serial.print(F("[HA command] "));
  Serial.print(name);
  Serial.print('=');
  Serial.println(value, 1);
}

void onNumberCommand(HANumeric number, HANumber* sender) {
  const float floatValue = number.toFloat();
  const long intValue = static_cast<long>(lroundf(floatValue));

  if (sender == &tempHighSetNumber) {
    state.tempHighSet = clampFloat(floatValue, -40.0f, 125.0f);
    printNumberCommand(F("temp_high_set"), state.tempHighSet);
  } else if (sender == &tempHighClearNumber) {
    state.tempHighClear = clampFloat(floatValue, -40.0f, 125.0f);
    printNumberCommand(F("temp_high_clear"), state.tempHighClear);
  } else if (sender == &tempLowSetNumber) {
    state.tempLowSet = clampFloat(floatValue, -40.0f, 125.0f);
    printNumberCommand(F("temp_low_set"), state.tempLowSet);
  } else if (sender == &tempLowClearNumber) {
    state.tempLowClear = clampFloat(floatValue, -40.0f, 125.0f);
    printNumberCommand(F("temp_low_clear"), state.tempLowClear);
  } else if (sender == &humHighSetNumber) {
    state.humHighSet = clampFloat(floatValue, 0.0f, 100.0f);
    printNumberCommand(F("hum_high_set"), state.humHighSet);
  } else if (sender == &humHighClearNumber) {
    state.humHighClear = clampFloat(floatValue, 0.0f, 100.0f);
    printNumberCommand(F("hum_high_clear"), state.humHighClear);
  } else if (sender == &humLowSetNumber) {
    state.humLowSet = clampFloat(floatValue, 0.0f, 100.0f);
    printNumberCommand(F("hum_low_set"), state.humLowSet);
  } else if (sender == &humLowClearNumber) {
    state.humLowClear = clampFloat(floatValue, 0.0f, 100.0f);
    printNumberCommand(F("hum_low_clear"), state.humLowClear);
  } else if (sender == &lightOnTimeNumber) {
    state.lightOnTimeMinutes = clampUInt16(intValue, 0, 1439);
    printNumberCommand(F("light_on_time_minutes"), state.lightOnTimeMinutes);
  } else if (sender == &lightOffTimeNumber) {
    state.lightOffTimeMinutes = clampUInt16(intValue, 0, 1439);
    printNumberCommand(F("light_off_time_minutes"), state.lightOffTimeMinutes);
  } else if (sender == &defaultLightDimMinutesNumber) {
    state.defaultLightDimMinutes = clampUInt16(intValue, 0, 1440);
    state.haDimDurationMinutes = state.defaultLightDimMinutes;
    printNumberCommand(F("light_dim_minutes"), state.defaultLightDimMinutes);
  } else if (sender == &soilAirNumber) {
    state.soilAir = static_cast<int16_t>(clampUInt16(intValue, 0, 1023));
    printNumberCommand(F("soil_air"), state.soilAir);
  } else if (sender == &soilWaterNumber) {
    state.soilWater = static_cast<int16_t>(clampUInt16(intValue, 0, 1023));
    printNumberCommand(F("soil_water"), state.soilWater);
  } else if (sender == &soilDepthNumber) {
    state.soilDepthMm = static_cast<int16_t>(clampUInt16(intValue, 0, 300));
    printNumberCommand(F("soil_depth_mm"), state.soilDepthMm);
  } else if (sender == &haDimTargetPercentNumber) {
    state.haDimTargetPercent = clampPercent(intValue);
    printNumberCommand(F("ha_dim_target_percent"), state.haDimTargetPercent);
  } else if (sender == &haDimDurationMinutesNumber) {
    state.haDimDurationMinutes = clampUInt16(intValue, 0, 1440);
    printNumberCommand(F("ha_dim_duration_minutes"), state.haDimDurationMinutes);
  }

  publishConfigValues();
}

void onSyncTimeButtonCommand(HAButton* /*sender*/) {
  Serial.println(F("[HA command] sync_time button pressed."));
  runNtpDiagnostics();
}

void onReadSoilRawButtonCommand(HAButton* /*sender*/) {
  Serial.println(F("[HA command] read_soil_raw_value button pressed."));
  state.soilRaw += 11;
  if (state.soilRaw > 760) {
    state.soilRaw = 420;
  }
  publishSensorValues();
}

void onStartHaDimButtonCommand(HAButton* /*sender*/) {
  Serial.println(F("[HA command] start_ha_dim button pressed."));
  state.growLightOn = state.haDimTargetPercent > 0;
  state.growLightBrightness = state.haDimTargetPercent;
  publishSwitchAndLightStates();
}

void configureHomeAssistantEntities() {
  device.setName(TEST_DEVICE_NAME);
  device.setManufacturer("Smaeenhouse");
  device.setModel("Nano 33 IoT Network Diagnostics");
  device.setSoftwareVersion("network-diagnostics-test");
  device.enableSharedAvailability();
  device.enableLastWill();

  mqtt.setDiscoveryPrefix(TEST_MQTT_PREFIX);

  temperatureSensor.setName("Temperature");
  temperatureSensor.setUnitOfMeasurement("C");
  humiditySensor.setName("Humidity");
  humiditySensor.setUnitOfMeasurement("%");
  soilPercentSensor.setName("Soil Moisture Percent");
  soilPercentSensor.setUnitOfMeasurement("%");
  soilRawSensor.setName("Soil Moisture Raw");
  cabinetIlluminanceSensor.setName("Cabinet Illuminance");
  cabinetIlluminanceSensor.setUnitOfMeasurement("lx");
  cabinetFullSpectrumSensor.setName("Cabinet Light Full Spectrum Raw");
  cabinetInfraredSensor.setName("Cabinet Light Infrared Raw");
  cabinetVisibleSensor.setName("Cabinet Light Visible Raw");
  fanRpmSensor.setName("Fan RPM");
  fanRpmSensor.setUnitOfMeasurement("rpm");
  lightFaultReasonSensor.setName("Light Fault Reason");

  lightFaultBinarySensor.setName("Light Fault");
  fanFaultBinarySensor.setName("Fan Fault");
  shtFaultBinarySensor.setName("SHT Fault");
  rtcFaultBinarySensor.setName("RTC Fault");
  eepromFaultBinarySensor.setName("EEPROM Fault");
  lightSensorFaultBinarySensor.setName("Light Sensor Fault");

  fanSwitch.setName("Fan");
  fanAutoModeSwitch.setName("Fan Auto Mode");
  lightAutoModeSwitch.setName("Light Auto Mode");
  lightHardPowerOffSwitch.setName("Light Hard Power Off");
  lightFallbackUseAutoModeSwitch.setName("Light Fallback To Auto");

  growLight.setName("Grow Light");
  growLight.setBrightnessScale(100);

  tempHighSetNumber.setName("Temp High Set");
  tempHighSetNumber.setUnitOfMeasurement("C");
  tempHighSetNumber.setMin(-40.0f);
  tempHighSetNumber.setMax(125.0f);
  tempHighSetNumber.setStep(0.1f);

  tempHighClearNumber.setName("Temp High Clear");
  tempHighClearNumber.setUnitOfMeasurement("C");
  tempHighClearNumber.setMin(-40.0f);
  tempHighClearNumber.setMax(125.0f);
  tempHighClearNumber.setStep(0.1f);

  tempLowSetNumber.setName("Temp Low Set");
  tempLowSetNumber.setUnitOfMeasurement("C");
  tempLowSetNumber.setMin(-40.0f);
  tempLowSetNumber.setMax(125.0f);
  tempLowSetNumber.setStep(0.1f);

  tempLowClearNumber.setName("Temp Low Clear");
  tempLowClearNumber.setUnitOfMeasurement("C");
  tempLowClearNumber.setMin(-40.0f);
  tempLowClearNumber.setMax(125.0f);
  tempLowClearNumber.setStep(0.1f);

  humHighSetNumber.setName("Hum High Set");
  humHighSetNumber.setUnitOfMeasurement("%");
  humHighSetNumber.setMin(0.0f);
  humHighSetNumber.setMax(100.0f);
  humHighSetNumber.setStep(0.1f);

  humHighClearNumber.setName("Hum High Clear");
  humHighClearNumber.setUnitOfMeasurement("%");
  humHighClearNumber.setMin(0.0f);
  humHighClearNumber.setMax(100.0f);
  humHighClearNumber.setStep(0.1f);

  humLowSetNumber.setName("Hum Low Set");
  humLowSetNumber.setUnitOfMeasurement("%");
  humLowSetNumber.setMin(0.0f);
  humLowSetNumber.setMax(100.0f);
  humLowSetNumber.setStep(0.1f);

  humLowClearNumber.setName("Hum Low Clear");
  humLowClearNumber.setUnitOfMeasurement("%");
  humLowClearNumber.setMin(0.0f);
  humLowClearNumber.setMax(100.0f);
  humLowClearNumber.setStep(0.1f);

  lightOnTimeNumber.setName("Light On Time Minutes");
  lightOnTimeNumber.setUnitOfMeasurement("min");
  lightOnTimeNumber.setMin(0.0f);
  lightOnTimeNumber.setMax(1439.0f);
  lightOnTimeNumber.setStep(1.0f);

  lightOffTimeNumber.setName("Light Off Time Minutes");
  lightOffTimeNumber.setUnitOfMeasurement("min");
  lightOffTimeNumber.setMin(0.0f);
  lightOffTimeNumber.setMax(1439.0f);
  lightOffTimeNumber.setStep(1.0f);

  defaultLightDimMinutesNumber.setName("Light Default Dim Minutes");
  defaultLightDimMinutesNumber.setUnitOfMeasurement("min");
  defaultLightDimMinutesNumber.setMin(0.0f);
  defaultLightDimMinutesNumber.setMax(1440.0f);
  defaultLightDimMinutesNumber.setStep(1.0f);

  soilAirNumber.setName("Soil Air");
  soilAirNumber.setMin(0.0f);
  soilAirNumber.setMax(1023.0f);
  soilAirNumber.setStep(1.0f);

  soilWaterNumber.setName("Soil Water");
  soilWaterNumber.setMin(0.0f);
  soilWaterNumber.setMax(1023.0f);
  soilWaterNumber.setStep(1.0f);

  soilDepthNumber.setName("Soil Depth mm");
  soilDepthNumber.setUnitOfMeasurement("mm");
  soilDepthNumber.setMin(0.0f);
  soilDepthNumber.setMax(300.0f);
  soilDepthNumber.setStep(1.0f);

  haDimTargetPercentNumber.setName("HA Dim Target Percent");
  haDimTargetPercentNumber.setUnitOfMeasurement("%");
  haDimTargetPercentNumber.setMin(0.0f);
  haDimTargetPercentNumber.setMax(100.0f);
  haDimTargetPercentNumber.setStep(1.0f);

  haDimDurationMinutesNumber.setName("HA Dim Duration Minutes");
  haDimDurationMinutesNumber.setUnitOfMeasurement("min");
  haDimDurationMinutesNumber.setMin(0.0f);
  haDimDurationMinutesNumber.setMax(1440.0f);
  haDimDurationMinutesNumber.setStep(1.0f);

  syncTimeButton.setName("Sync Time");
  readSoilRawButton.setName("Read Soil Raw Value");
  startHaDimButton.setName("Start HA Dim");

  fanSwitch.onCommand(onFanSwitchCommand);
  fanAutoModeSwitch.onCommand(onFanAutoModeCommand);
  lightAutoModeSwitch.onCommand(onLightAutoModeCommand);
  lightHardPowerOffSwitch.onCommand(onLightHardPowerOffCommand);
  lightFallbackUseAutoModeSwitch.onCommand(onLightFallbackCommand);
  growLight.onStateCommand(onLightStateCommand);
  growLight.onBrightnessCommand(onLightBrightnessCommand);

  tempHighSetNumber.onCommand(onNumberCommand);
  tempHighClearNumber.onCommand(onNumberCommand);
  tempLowSetNumber.onCommand(onNumberCommand);
  tempLowClearNumber.onCommand(onNumberCommand);
  humHighSetNumber.onCommand(onNumberCommand);
  humHighClearNumber.onCommand(onNumberCommand);
  humLowSetNumber.onCommand(onNumberCommand);
  humLowClearNumber.onCommand(onNumberCommand);
  lightOnTimeNumber.onCommand(onNumberCommand);
  lightOffTimeNumber.onCommand(onNumberCommand);
  defaultLightDimMinutesNumber.onCommand(onNumberCommand);
  soilAirNumber.onCommand(onNumberCommand);
  soilWaterNumber.onCommand(onNumberCommand);
  soilDepthNumber.onCommand(onNumberCommand);
  haDimTargetPercentNumber.onCommand(onNumberCommand);
  haDimDurationMinutesNumber.onCommand(onNumberCommand);

  syncTimeButton.onCommand(onSyncTimeButtonCommand);
  readSoilRawButton.onCommand(onReadSoilRawButtonCommand);
  startHaDimButton.onCommand(onStartHaDimButtonCommand);
}

void handleSerialCommand() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == '\r' || command == '\n') {
      continue;
    }

    switch (command) {
      case 'h':
        printHelp();
        break;
      case 'w':
        runNetworkDiagnostics();
        break;
      case 'n':
        runNtpDiagnostics();
        break;
      case 'm':
        beginMqtt();
        break;
      case 'p':
        publishAllStates();
        break;
      case 'r':
        rotateSyntheticValues();
        publishAllStates();
        break;
      case 'a':
        diagnosticAvailable = !diagnosticAvailable;
        device.setAvailability(mqtt.isConnected() && diagnosticAvailable);
        Serial.print(F("[HA] Diagnostic availability="));
        Serial.println(diagnosticAvailable ? F("ON") : F("OFF"));
        break;
      default:
        Serial.print(F("Unknown command: "));
        Serial.println(command);
        printHelp();
        break;
    }
  }
}

void serviceMqtt(uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    if (wasMqttConnected) {
      device.setAvailability(false);
      mqtt.disconnect();
      wasMqttConnected = false;
      Serial.println(F("[MQTT] Disconnected because WiFi is down."));
    }
    return;
  }

  mqtt.loop();

  const bool mqttConnected = mqtt.isConnected();
  if (mqttConnected && !wasMqttConnected) {
    Serial.println(F("[MQTT] Connected to broker."));
    device.setAvailability(diagnosticAvailable);
    publishAllStates();
  } else if (!mqttConnected && wasMqttConnected) {
    Serial.print(F("[MQTT] Disconnected, state="));
    Serial.println(static_cast<int>(mqtt.getState()));
    device.setAvailability(false);
  }
  wasMqttConnected = mqttConnected;

  if (!mqttConnected && (nowMs - lastMqttReconnectAttemptMs) >= MQTT_RECONNECT_INTERVAL_MS) {
    lastMqttReconnectAttemptMs = nowMs;
    beginMqtt();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 4000UL) {
  }

  Serial.println();
  Serial.println(F("NetworkDiagnosticsTest starting."));
  printHelp();

  configureHomeAssistantEntities();
  runNetworkDiagnostics();
  runNtpDiagnostics();
  beginMqtt();
  printBriefStatus();
}

void loop() {
  const uint32_t nowMs = millis();

  handleSerialCommand();
  serviceMqtt(nowMs);

  if ((nowMs - lastStatusPrintMs) >= STATUS_PRINT_INTERVAL_MS) {
    lastStatusPrintMs = nowMs;
    printBriefStatus();
  }
}
