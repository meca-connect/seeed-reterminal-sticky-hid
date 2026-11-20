/*
 * s4_dot_follow — verify the portrait model end to end.
 *
 * Touch (raw == logical portrait) drives a dot drawn at the same logical
 * position through the 90-degree display transpose. If the model is right,
 * the dot appears exactly under your finger. Tap 3 spots (anywhere); each tap
 * prints the raw coords and draws the dot there. No BLE, no gestures.
 */
#include "../_common/sticky_hw.h"

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S4 DOT FOLLOW — tap 3 spots; dot should appear under your finger");
  Serial.printf("Touch: %s\n", stickyTouchBegin() ? "OK" : "FAILED");
  Serial.printf("Display: %s\n", stickyDisplayBegin() ? "OK" : "FAILED");
  stickyDisplayClear(); stickyDisplayRefresh();
  Serial.println("READY — tap anywhere; dot should land exactly where you touched.");
}

void loop() {
  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  if (n == 1) {
    // raw == logical portrait; draw the dot right there.
    stickyDisplayClear();
    stickyDrawDot(pts[0].x, pts[0].y, 18);
    stickyDisplayRefresh();
    Serial.printf("DOT at logical=(%u,%u)\n", pts[0].x, pts[0].y);
    while (stickyTouchRead(pts,5) != 0) delay(10); // wait for lift
  }
  delay(5);
}
