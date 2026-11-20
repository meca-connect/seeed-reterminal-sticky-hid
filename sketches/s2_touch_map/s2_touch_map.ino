/*
 * s2_touch_map — calibrate the raw-touch -> display coordinate rotation.
 *
 * The GT911 reports 480x800 (portrait); the panel is 800x480 (landscape), so a
 * 90-degree rotation (and possibly a flip) connects them. This sketch draws a
 * dot where it THINKS your finger is, and lets you switch among 8 mappings over
 * serial until the dot lands under your finger. No BLE, no gestures.
 *
 * Serial:  M0..M7   pick a mapping (default M0)
 *          C        clear screen
 * Tap once; a dot draws at the mapped position and serial prints raw + mapped.
 * Send M1, M2, ... until the dot appears where you actually touched.
 * Report the mapping that tracks your finger; we bake it into the next sketch.
 */
#include "../_common/sticky_hw.h"

static uint8_t mapping = 0;

// raw x in [0,480), y in [0,800) -> logical display (dx in [0,800), dy in [0,480))
static void mapPoint(uint16_t rx, uint16_t ry, int &dx, int &dy, uint8_t m) {
  // Candidate family: raw long axis (y, 0..799) -> display long axis (x, 0..799);
  // raw short axis (x, 0..479) -> display short axis (y, 0..479), with flips.
  int lx = ry;         // 0..799 (long)
  int sx = rx;         // 0..479 (short)
  switch (m & 3) {
    case 0: dx = lx;        dy = sx;       break;  // rotate 90 CW, no flip
    case 1: dx = 799 - lx;  dy = sx;       break;  // + horizontal flip
    case 2: dx = lx;        dy = 479 - sx; break;  // + vertical flip
    default:dx = 799 - lx;  dy = 479 - sx; break;  // + both flips
  }
  // Fallback family: raw short axis -> display long axis is geometrically wrong
  // (480 < 800), but kept as M4..M7 in case the register X/Y is swapped on some
  // panel revision. These stretch the short axis to fill the long side.
  if (m >= 4) {
    int lx2 = (int)((uint32_t)rx * 800 / 480);
    int sx2 = (int)((uint32_t)ry * 480 / 800);
    switch (m & 3) {
      case 0: dx = lx2;       dy = sx2;       break;
      case 1: dx = 799 - lx2; dy = sx2;       break;
      case 2: dx = lx2;       dy = 479 - sx2; break;
      default:dx = 799 - lx2; dy = 479 - sx2; break;
    }
  }
}

static void statusLine(uint16_t rx, uint16_t ry, int dx, int dy) {
  Serial.printf("RAW x=%u y=%u  ->  DISP x=%d y=%d   [M%u]\n", rx, ry, dx, dy, mapping);
}

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S2 TOUCH MAP — tap to draw a dot, M0..M7 to find the mapping");
  Serial.printf("Touch: %s\n", stickyTouchBegin() ? "OK" : "FAILED");
  Serial.printf("Display: %s\n", stickyDisplayBegin() ? "OK" : "FAILED");
  stickyDisplayClear(); stickyDisplayRefresh();
  Serial.println("READY (M0). Tap anywhere, then send M1..M7 until the dot is under your finger.");
}

void loop() {
  // serial mapping selector
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c=='M'||c=='m') {
      while (!Serial.available()) delay(1);
      char d=(char)Serial.read();
      if (d>='0'&&d<='7') { mapping=d-'0'; stickyDisplayClear(); stickyDisplayRefresh(); Serial.printf("mapping=M%u, cleared\n",mapping); }
    } else if (c=='C'||c=='c') {
      stickyDisplayClear(); stickyDisplayRefresh(); Serial.println("cleared");
    } else if (c=='\n'||c=='\r') {}
    else { Serial.printf("? '%c' (M0..M7, C)\n", c); }
  }

  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  if (n == 1) { // single fresh touch-down
    int dx, dy;
    mapPoint(pts[0].x, pts[0].y, dx, dy, mapping);
    stickyDisplayClear();
    stickyDrawDot(dx, dy, 14);
    stickyDisplayRefresh();
    statusLine(pts[0].x, pts[0].y, dx, dy);
    while (stickyTouchRead(pts,5) != 0) delay(10); // wait for lift
  }
  delay(5);
}
