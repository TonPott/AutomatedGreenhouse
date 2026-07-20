#include <Wire.h>
#include <Adafruit_TSL2591.h>

// Minimal bench test for the CQrobot CQRTSL25911 / TSL25911 light sensor.
// Production firmware currently polls the sensor and only prepares the INT line.

constexpr uint8_t TSL2591_I2C_ADDRESS = 0x29;
constexpr uint8_t PIN_LIGHT_SENSOR_INT = 9;
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000UL;

Adafruit_TSL2591 lightSensor(2591);

uint32_t lastSampleMs = 0;

void printSample()
{
  const uint32_t luminosity = lightSensor.getFullLuminosity();
  const uint16_t infrared = static_cast<uint16_t>(luminosity >> 16);
  const uint16_t fullSpectrum = static_cast<uint16_t>(luminosity & 0xFFFF);
  const uint16_t visible = (fullSpectrum >= infrared) ? (fullSpectrum - infrared) : 0;
  const float lux = lightSensor.calculateLux(fullSpectrum, infrared);

  Serial.println();
  Serial.println(F("=== CQRTSL25911 sample ==="));
  Serial.print(F("Lux = "));
  Serial.println(lux, 2);
  Serial.print(F("Full spectrum raw = "));
  Serial.println(fullSpectrum);
  Serial.print(F("Infrared raw = "));
  Serial.println(infrared);
  Serial.print(F("Visible raw = "));
  Serial.println(visible);
  Serial.print(F("INT pin D9 = "));
  Serial.println(digitalRead(PIN_LIGHT_SENSOR_INT) == HIGH ? F("HIGH") : F("LOW"));
}

void setup()
{
  Serial.begin(115200);
  while (!Serial && millis() < 4000) {}

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("CQRTSL25911 / TSL25911 light sensor test"));
  Serial.println(F("========================================"));
  Serial.print(F("Expected I2C address: 0x"));
  Serial.println(TSL2591_I2C_ADDRESS, HEX);
  Serial.print(F("Prepared INT pin: D"));
  Serial.println(PIN_LIGHT_SENSOR_INT);

  pinMode(PIN_LIGHT_SENSOR_INT, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(100000);

  if (!lightSensor.begin(&Wire, TSL2591_I2C_ADDRESS))
  {
    Serial.println(F("Sensor not found. Check wiring, power, pull-ups, and I2C address."));
    while (true)
    {
      delay(1000);
    }
  }

  lightSensor.setGain(TSL2591_GAIN_LOW);
  lightSensor.setTiming(TSL2591_INTEGRATIONTIME_100MS);
  lightSensor.clearInterrupt();

  Serial.println(F("Sensor initialized."));
  Serial.println(F("Move the sensor between dark, ambient, and grow-light conditions."));
  printSample();
  lastSampleMs = millis();
}

void loop()
{
  const uint32_t nowMs = millis();
  if (nowMs - lastSampleMs >= SAMPLE_INTERVAL_MS)
  {
    lastSampleMs = nowMs;
    printSample();
  }
}
