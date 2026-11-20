#pragma once
// Buzzer (GPIO48, LEDC) and BQ27220 fuel gauge (I2C1 SDA=1 SCL=0, 0x55).
// Pins/addresses from Seeed's Sticky_peripheral_demo pin_config.h.
#include <Arduino.h>
#include <Wire.h>

// ---------------- buzzer: non-blocking tone sequences ----------------
namespace buzz {
struct Step { uint16_t hz, ms; };   // hz = 0 -> silence
static Step q[8];
static uint8_t qn = 0, qi = 0;
static uint32_t stepEnd = 0;
static bool enabled = true, ready = false;
static const uint8_t PIN = 48;

static void out(uint16_t hz) {
  if (!ready) return;
  if (hz) { ledcChangeFrequency(PIN, hz, 10); ledcWrite(PIN, 384); }   // ~37% duty
  else ledcWrite(PIN, 0);
}
static void begin() { ready = ledcAttach(PIN, 2700, 10); if (ready) ledcWrite(PIN, 0); }
static void play(std::initializer_list<Step> steps, bool force = false) {
  if (!enabled && !force) return;
  qn = 0;
  for (const Step& s : steps) if (qn < 8) q[qn++] = s;
  qi = 0; out(q[0].hz); stepEnd = millis() + q[0].ms;
}
static void service() {
  if (qi >= qn) return;
  if (int32_t(millis() - stepEnd) < 0) return;
  if (++qi >= qn) { out(0); return; }
  out(q[qi].hz); stepEnd = millis() + q[qi].ms;
}
// The sound palette: low, short "tocks" instead of shrill beeps. A buzzer is
// loudest/harshest near its ~2.7 kHz resonance, so everything sits far below it.
static void click()      { play({{440, 10}}); }
static void dbl()        { play({{440, 8}, {0, 40}, {523, 8}}); }
static void right()      { play({{330, 14}}); }
static void middle()     { play({{392, 12}}); }
static void dragStart()  { play({{294, 16}}); }
static void key()        { play({{494, 6}}); }
static void open()       { play({{330, 14}, {0, 18}, {440, 18}}); }
static void connected()  { play({{330, 40}, {0, 20}, {440, 60}}); }
static void lost()       { play({{440, 40}, {0, 20}, {330, 60}}); }
static void sleep()      { play({{392, 30}, {0, 20}, {262, 60}}, true); }
static void warn()       { play({{220, 120}}, true); }
}  // namespace buzz

// ---------------- battery gauge ----------------
namespace batt {
static bool ok = false;
static int pct = -1, mv = 0, ma = 0;
static uint32_t lastRead = 0;
static bool readWord(uint8_t reg, uint16_t& v) {
  Wire1.beginTransmission(0x55); Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(0x55, 2) != 2) return false;
  v = uint16_t(Wire1.read()) | uint16_t(Wire1.read()) << 8;
  return true;
}
static void sample() {
  uint16_t soc, v, c;
  ok = readWord(0x2C, soc) && readWord(0x08, v) && readWord(0x0C, c);
  if (ok) { pct = soc > 100 ? 100 : soc; mv = v; ma = int16_t(c); }
  lastRead = millis();
}
// BQ25616 charger: EN_BAT_CHGn (GPIO39) must be driven LOW or the battery
// never charges (Seeed demo_charger.cpp). STAT (GPIO40) reads LOW while charging.
static void begin() {
  pinMode(39, OUTPUT); digitalWrite(39, LOW);
  pinMode(40, INPUT_PULLUP);
  Wire1.begin(1, 0, 100000); Wire1.setTimeOut(20); sample();
}
static bool chargerActive() { return digitalRead(40) == LOW; }
static bool charging() { return chargerActive(); }   // gauge current scaling unverified (read 5750 mA)
// Returns true when a new sample was taken.
static bool service(uint32_t periodMs = 30000) {
  if (millis() - lastRead < periodMs) return false;
  sample(); return true;
}
}  // namespace batt
