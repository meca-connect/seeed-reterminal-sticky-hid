/*
 * s2b_calibrate — autonomous 4-corner touch calibration.
 *
 * Draws one big target dot at a time in 4 known logical corners. You touch the
 * dot you see (wherever it physically appears); the sketch records the raw
 * GT911 coordinate for that corner, then advances. After all 4 it prints a
 * summary we use to compute the exact raw->display rotation + flip + scale.
 *
 * Follow the on-screen dots; no serial typing needed.
 */
#include "../_common/sticky_hw.h"

static const int CORNERS[4][2] = {
  {120, 100},   // 0 top-left
  {680, 100},   // 1 top-right
  {120, 380},   // 2 bottom-left
  {680, 380}    // 3 bottom-right
};
static int corner = 0;
static uint16_t rawX[4], rawY[4];
static bool gotRaw[4];

static bool waitForTap(uint16_t &rx, uint16_t &ry) {
  // Wait for a fresh touch-down, capture primary point, then wait for lift.
  for (;;) {
    StickyTouchPoint pts[5]{};
    int n = stickyTouchRead(pts, 5);
    if (n > 0) {
      rx = pts[0].x; ry = pts[0].y;
      uint32_t t0 = millis();
      while (millis() - t0 < 5000) {
        if (stickyTouchRead(pts,5) == 0) return true; // released
        delay(10);
      }
      return false;
    }
    delay(10);
  }
}

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S2B CALIBRATE — touch each dot as it appears (4 corners)");
  Serial.printf("Touch: %s\n", stickyTouchBegin() ? "OK" : "FAILED");
  Serial.printf("Display: %s\n", stickyDisplayBegin() ? "OK" : "FAILED");
  stickyDisplayClear();
  stickyDrawDot(CORNERS[0][0], CORNERS[0][1], 24);
  stickyDisplayRefresh();
  Serial.printf("Touch the DOT for corner %d (logical %d,%d)...\n", corner, CORNERS[0][0], CORNERS[0][1]);
}

void loop() {
  if (corner >= 4) return;
  uint16_t rx=0, ry=0;
  if (!waitForTap(rx, ry)) {
    Serial.printf("corner %d timed out waiting for release, retry\n", corner);
    stickyDrawDot(CORNERS[corner][0], CORNERS[corner][1], 24);
    stickyDisplayRefresh();
    return;
  }
  rawX[corner]=rx; rawY[corner]=ry; gotRaw[corner]=true;
  Serial.printf("CORNER %d logical=(%d,%d) raw=(%u,%u)\n", corner, CORNERS[corner][0], CORNERS[corner][1], rx, ry);
  ++corner;
  if (corner < 4) {
    delay(300);
    stickyDisplayClear();
    stickyDrawDot(CORNERS[corner][0], CORNERS[corner][1], 24);
    stickyDisplayRefresh();
    Serial.printf("Touch the DOT for corner %d (logical %d,%d)...\n", corner, CORNERS[corner][0], CORNERS[corner][1]);
  } else {
    delay(300);
    stickyDisplayClear(); stickyDisplayRefresh();
    Serial.println("CALIBRATION DONE");
    for (int i=0;i<4;++i)
      Serial.printf("  corner %d logical=(%d,%d) raw=(%u,%u)\n", i, CORNERS[i][0], CORNERS[i][1], rawX[i], rawY[i]);
  }
}
