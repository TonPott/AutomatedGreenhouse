#include <Wire.h>
#include <AD5263.h>

// --------------------------------------------------
// Bench test for AD5263 + Arduino Nano 33 IoT
// Fixed settle delay for every relevant operation.
// --------------------------------------------------

constexpr uint8_t AD5263_ADDR = 0x2C;
constexpr uint8_t PIN_SHDN    = 4;

// Project channel mapping
constexpr uint8_t RDAC_W1 = 0;
constexpr uint8_t RDAC_W2 = 1;

// Fixed pause after every relevant state change
constexpr uint32_t LAMP_SETTLE_MS = 3000UL;

// Project brightness mapping reference points
constexpr uint8_t PCT_0_W1   = 255;
constexpr uint8_t PCT_0_W2   = 0;
constexpr uint8_t PCT_50_W1  = 0;
constexpr uint8_t PCT_50_W2  = 0;
constexpr uint8_t PCT_100_W1 = 0;
constexpr uint8_t PCT_100_W2 = 255;

AD5263 pot(AD5263_ADDR, &Wire);

bool shdnEnabled = false;  // false = D4 LOW = shutdown active

// --------------------------------------------------
// Helpers
// --------------------------------------------------

void waitForSettle(const __FlashStringHelper* reason)
{
  Serial.println();
  Serial.print(F("Waiting "));
  Serial.print(LAMP_SETTLE_MS);
  Serial.print(F(" ms for settle: "));
  Serial.println(reason);
  delay(LAMP_SETTLE_MS);
}

void setShdn(bool enable)
{
  shdnEnabled = enable;
  digitalWrite(PIN_SHDN, enable ? HIGH : LOW);

  Serial.println();
  Serial.println(F("=== SHDN state ==="));
  Serial.print(F("D4 = "));
  Serial.println(enable ? F("HIGH") : F("LOW"));
  Serial.print(F("AD5263 path = "));
  Serial.println(enable ? F("ACTIVE") : F("SHUTDOWN"));

  waitForSettle(F("SHDN change"));
}

bool ensureConnected()
{
  bool ok = pot.isConnected();
  Serial.println();
  Serial.println(F("=== Connection check ==="));
  Serial.print(F("Device at 0x"));
  Serial.print(AD5263_ADDR, HEX);
  Serial.print(F(": "));
  Serial.println(ok ? F("FOUND") : F("NOT FOUND"));
  return ok;
}

void printReadback()
{
  uint8_t w1 = pot.read(RDAC_W1);
  uint8_t w2 = pot.read(RDAC_W2);

  Serial.println();
  Serial.println(F("=== Readback ==="));
  Serial.print(F("W1 (RDAC 0) = "));
  Serial.println(w1);
  Serial.print(F("W2 (RDAC 1) = "));
  Serial.println(w2);
  Serial.print(F("Raw register readBackRegister() = 0x"));
  Serial.println(pot.readBackRegister(), HEX);
}

bool writePair(uint8_t w1, uint8_t w2, bool verify = true)
{
  Serial.println();
  Serial.println(F("=== Write pair ==="));
  Serial.print(F("Target W1 = "));
  Serial.println(w1);
  Serial.print(F("Target W2 = "));
  Serial.println(w2);

  uint8_t s1 = pot.write(RDAC_W1, w1);
  uint8_t s2 = pot.write(RDAC_W2, w2);

  Serial.print(F("write(W1) status = "));
  Serial.println(s1);
  Serial.print(F("write(W2) status = "));
  Serial.println(s2);

  bool ok = (s1 == 0 && s2 == 0);

  if (verify)
  {
    uint8_t rb1 = pot.read(RDAC_W1);
    uint8_t rb2 = pot.read(RDAC_W2);

    Serial.print(F("Readback W1 = "));
    Serial.println(rb1);
    Serial.print(F("Readback W2 = "));
    Serial.println(rb2);

    bool match = (rb1 == w1 && rb2 == w2);
    Serial.print(F("Verify = "));
    Serial.println(match ? F("OK") : F("MISMATCH"));

    ok = ok && match;
  }

  Serial.print(F("Result = "));
  Serial.println(ok ? F("PASS") : F("FAIL"));

  waitForSettle(F("new RDAC values"));
  return ok;
}

bool applyProjectPercent(uint8_t percent)
{
  if (percent > 100) percent = 100;

  uint8_t w1 = 0;
  uint8_t w2 = 0;

  if (percent <= 50)
  {
    // 0..50%: W1 moves 255 -> 0, W2 stays 0.
    w1 = map(percent, 0, 50, 255, 0);
    w2 = 0;
  }
  else
  {
    // 50..100%: W2 moves 0 -> 255, W1 stays 0.
    w2 = map(percent, 50, 100, 0, 255);
    w1 = 0;
  }

  Serial.println();
  Serial.println(F("=== Project percent mapping ==="));
  Serial.print(F("Percent = "));
  Serial.print(percent);
  Serial.println(F("%"));

  return writePair(w1, w2, true);
}

void runQuickProjectSequence()
{
  Serial.println();
  Serial.println(F("########################################"));
  Serial.println(F("# Quick project sequence"));
  Serial.println(F("########################################"));

  setShdn(false);

  if (!ensureConnected())
  {
    Serial.println(F("Abort: device not reachable."));
    return;
  }

  setShdn(true);

  Serial.println(F("1) Library reset() -> should go to midpoint"));
  uint8_t r = pot.reset();
  Serial.print(F("reset() status = "));
  Serial.println(r);
  printReadback();
  waitForSettle(F("reset()"));

  Serial.println(F("2) Project 0%  => W2=0, W1=255"));
  writePair(PCT_0_W1, PCT_0_W2, true);

  Serial.println(F("3) Project 50% => W2=0, W1=0"));
  writePair(PCT_50_W1, PCT_50_W2, true);

  Serial.println(F("4) Project 100% => W2=255, W1=0"));
  writePair(PCT_100_W1, PCT_100_W2, true);

  Serial.println(F("5) SHDN LOW test"));
  setShdn(false);

  Serial.println(F("6) SHDN HIGH again"));
  setShdn(true);
  printReadback();
}

void printHelp()
{
  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("AD5263 bench test commands"));
  Serial.println(F("========================================"));
  Serial.println(F("h        -> help"));
  Serial.println(F("c        -> connection check"));
  Serial.println(F("i        -> device info + readback"));
  Serial.println(F("q        -> run quick project sequence"));
  Serial.println(F("e        -> enable SHDN (D4 HIGH)"));
  Serial.println(F("x        -> disable SHDN (D4 LOW)"));
  Serial.println(F("r        -> pot.reset()"));
  Serial.println(F("z        -> pot.zeroAll()"));
  Serial.println(F("m        -> midScaleReset W1 and W2"));
  Serial.println(F("0        -> apply project 0%"));
  Serial.println(F("5        -> apply project 50%"));
  Serial.println(F("1        -> apply project 100%"));
  Serial.println(F("pNN      -> apply project percent, e.g. p37"));
  Serial.println(F("w1=NN    -> set W1 directly, e.g. w1=128"));
  Serial.println(F("w2=NN    -> set W2 directly, e.g. w2=200"));
  Serial.println(F("a=NN,MM  -> set pair W1,W2 directly, e.g. a=0,255"));
  Serial.println(F("u        -> print readback"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("All relevant state changes wait for LAMP_SETTLE_MS"));
  Serial.print(F("Current LAMP_SETTLE_MS = "));
  Serial.print(LAMP_SETTLE_MS);
  Serial.println(F(" ms"));
  Serial.println(F("========================================"));
}

void handleCommand(String cmd)
{
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "h")
  {
    printHelp();
    return;
  }

  if (cmd == "c")
  {
    ensureConnected();
    return;
  }

  if (cmd == "i")
  {
    Serial.println();
    Serial.println(F("=== Device info ==="));
    Serial.print(F("I2C address = 0x"));
    Serial.println(pot.getAddress(), HEX);
    Serial.print(F("isConnected() = "));
    Serial.println(pot.isConnected() ? F("YES") : F("NO"));
    Serial.print(F("SHDN state = "));
    Serial.println(shdnEnabled ? F("ACTIVE") : F("SHUTDOWN"));
    printReadback();
    return;
  }

  if (cmd == "q")
  {
    runQuickProjectSequence();
    return;
  }

  if (cmd == "e")
  {
    setShdn(true);
    return;
  }

  if (cmd == "x")
  {
    setShdn(false);
    return;
  }

  if (cmd == "r")
  {
    Serial.println();
    Serial.println(F("=== reset() ==="));
    uint8_t s = pot.reset();
    Serial.print(F("status = "));
    Serial.println(s);
    printReadback();
    waitForSettle(F("reset()"));
    return;
  }

  if (cmd == "z")
  {
    Serial.println();
    Serial.println(F("=== zeroAll() ==="));
    uint8_t s = pot.zeroAll();
    Serial.print(F("status = "));
    Serial.println(s);
    printReadback();
    waitForSettle(F("zeroAll()"));
    return;
  }

  if (cmd == "m")
  {
    Serial.println();
    Serial.println(F("=== midScaleReset() on W1 + W2 ==="));
    uint8_t s1 = pot.midScaleReset(RDAC_W1);
    uint8_t s2 = pot.midScaleReset(RDAC_W2);
    Serial.print(F("W1 status = "));
    Serial.println(s1);
    Serial.print(F("W2 status = "));
    Serial.println(s2);
    printReadback();
    waitForSettle(F("midScaleReset()"));
    return;
  }

  if (cmd == "0")
  {
    writePair(PCT_0_W1, PCT_0_W2, true);
    return;
  }

  if (cmd == "5")
  {
    writePair(PCT_50_W1, PCT_50_W2, true);
    return;
  }

  if (cmd == "1")
  {
    writePair(PCT_100_W1, PCT_100_W2, true);
    return;
  }

  if (cmd.startsWith("p"))
  {
    int value = cmd.substring(1).toInt();
    value = constrain(value, 0, 100);
    applyProjectPercent((uint8_t)value);
    return;
  }

  if (cmd.startsWith("w1="))
  {
    int value = cmd.substring(3).toInt();
    value = constrain(value, 0, 255);
    writePair((uint8_t)value, pot.read(RDAC_W2), true);
    return;
  }

  if (cmd.startsWith("w2="))
  {
    int value = cmd.substring(3).toInt();
    value = constrain(value, 0, 255);
    writePair(pot.read(RDAC_W1), (uint8_t)value, true);
    return;
  }

  if (cmd.startsWith("a="))
  {
    int comma = cmd.indexOf(',');
    if (comma > 2)
    {
      int v1 = cmd.substring(2, comma).toInt();
      int v2 = cmd.substring(comma + 1).toInt();
      v1 = constrain(v1, 0, 255);
      v2 = constrain(v2, 0, 255);
      writePair((uint8_t)v1, (uint8_t)v2, true);
      return;
    }
  }

  if (cmd == "u")
  {
    printReadback();
    return;
  }

  Serial.println();
  Serial.print(F("Unknown command: "));
  Serial.println(cmd);
  Serial.println(F("Type 'h' for help."));
}

// --------------------------------------------------
// Arduino
// --------------------------------------------------

void setup()
{
  pinMode(PIN_SHDN, OUTPUT);
  digitalWrite(PIN_SHDN, LOW);   // keep shutdown active during boot

  Serial.begin(115200);
  while (!Serial && millis() < 4000) {}

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("AD5263 project bench test"));
  Serial.println(F("Nano 33 IoT + external SHDN on D4"));
  Serial.println(F("========================================"));

  Wire.begin();
  Wire.setClock(100000);

  Serial.println(F("Wire.begin() done"));
  Serial.print(F("SHDN default = "));
  Serial.println(F("LOW (shutdown active)"));

  bool found = pot.begin();
  Serial.print(F("pot.begin() = "));
  Serial.println(found ? F("true") : F("false"));

  ensureConnected();
  printHelp();
}

void loop()
{
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }
}
