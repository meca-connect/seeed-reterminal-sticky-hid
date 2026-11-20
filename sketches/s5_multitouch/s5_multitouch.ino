/*
 * s5_multitouch — verify 2/3/4/5-finger detection.
 *
 * Reports every fresh frame over serial (count + per-contact id/x/y/size) and
 * draws one dot per contact, refreshing the panel only when the contact count
 * changes (so the slow e-paper doesn't throttle the sampling). Serial is the
 * authoritative evidence; the dots are a visual aid. No BLE, no gestures.
 *
 * Put N fingers down together, hold a moment, lift. Watch serial for count=N
 * with N distinct ids in ONE frame, and check the panel shows N dots.
 */
#include "../_common/sticky_hw.h"

static uint8_t shownCount = 0xFF;
static uint8_t peak = 0;

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S5 MULTITOUCH — put 2,3,4,5 fingers down together and hold");
  Serial.printf("Touch: %s\n", stickyTouchBegin() ? "OK" : "FAILED");
  Serial.printf("Display: %s\n", stickyDisplayBegin() ? "OK" : "FAILED");
  stickyDisplayClear(); stickyDisplayRefresh();
  Serial.println("READY");
}

void loop() {
  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  if (n < 0) { delay(5); return; }

  if (n > peak) peak = n;
  Serial.printf("FRAME count=%d", n);
  for (int i=0;i<n;++i)
    Serial.printf(" [id=%u x=%u y=%u size=%u]", pts[i].id, pts[i].x, pts[i].y, pts[i].size);
  Serial.printf("  peak=%u\n", peak);

  // Redraw only when the visible count changes, to spare the slow e-paper.
  if (n != shownCount) {
    shownCount = n;
    stickyDisplayClear();
    for (int i=0;i<n;++i) stickyDrawDot(pts[i].x, pts[i].y, 20);
    stickyDisplayRefresh();
    Serial.printf("DOTS drawn: %d\n", n);
  }
  delay(5);
}
