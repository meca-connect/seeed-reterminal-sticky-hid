/*
 * s12_mvp — Sticky Trackpad MVP (docs/DESIGN.md, phase P1).
 *
 * Screens (R8: mouse only):
 *   TRACKPAD  static dot grid, dashed LEFT + RIGHT scroll strips, status dot
 *             top-centre (filled = connected, ring = not). Drawn once per
 *             entry / connect change; never refreshed in the touch path (R5).
 *   SETTINGS  connection status + bond count, three big buttons:
 *             ROTATE (0/90/180/270, saved in NVS, R7), FORGET HOST (tap twice
 *             within 5 s, R6), BACK.
 *   Any physical button (short press) toggles TRACKPAD <-> SETTINGS. The
 *   POWER/OK button (GPIO4) long press is left alone: the PMIC owns it.
 *
 * Gestures on TRACKPAD (GestureEngine v0.3, rotated screen space):
 *   1F move = pointer, tap = left click, long-press / double-tap = drag
 *   2F drag = scroll both axes (R2), 2F tap = right click, 3F tap = middle
 *   1F drag inside a side strip = horizontal scroll, drag up = right (R3)
 *
 * Transport: BLE HID mouse only (stock USB-C is the CH343P UART). Fast-interval
 * request and 400 kHz GT911 carried over from s11 (R4 baseline ~95 Hz/11.25 ms).
 *
 * Serial (115200): PING STATUS STATS RESET-STATS WATCH n FORGET ROTATE n
 *                  EDGE px SIGN +1|-1 SCREEN T|S
 * FQBN: esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc,FlashMode=dio
 */
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include "../_common/sticky_hw.h"
#include "../_common/font5x7.h"
#include "../../sticky_trackpad/GestureEngine.h"
#include "../../sticky_trackpad/MvpUi.h"

using stickymvp::Rotation;
using stickymvp::SettingsLayout;
using stickymvp::Screen;
using stickymvp::SCR_TRACKPAD;
using stickymvp::SCR_SETTINGS;

// ---------------- persistent settings ----------------
static Preferences prefs;
static Rotation rot;
static stickyapp::Tuning tuning;

static void applyTuning();
static void loadSettings() {
  prefs.begin("stickymvp", false);
  rot.r = prefs.getUChar("rot", 0) & 3;
  tuning.edgeWidthPx = prefs.getFloat("edge", 56.0f);
  tuning.sideStripSign = prefs.getFloat("sign", 1.0f) < 0 ? -1.0f : 1.0f;
  tuning.sideStripHorizontal = true;
  tuning.bottomStrip = false;
}
static void saveSettings() {
  prefs.putUChar("rot", rot.r);
  prefs.putFloat("edge", tuning.edgeWidthPx);
  prefs.putFloat("sign", tuning.sideStripSign);
}

// ---------------- BLE HID mouse ----------------
// Report: buttons(1) x(1) y(1) wheel(1) pan(1)
static uint8_t REPORT_MAP[] = {
  0x05,0x01, 0x09,0x02, 0xA1,0x01,   // Generic Desktop, Mouse
  0x09,0x01, 0xA1,0x00,               // Pointer
  0x05,0x09, 0x19,0x01, 0x29,0x03,   // Buttons 1..3
  0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
  0x95,0x01, 0x75,0x05, 0x81,0x03,   // padding
  0x05,0x01, 0x09,0x30, 0x09,0x31,   // X, Y
  0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
  0x09,0x38,                          // Wheel
  0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01, 0x81,0x06,
  0x05,0x0C, 0x0A,0x38,0x02,          // AC Pan (horizontal scroll)
  0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01, 0x81,0x06,
  0xC0, 0xC0
};

static NimBLEHIDDevice* hid = nullptr;
static NimBLECharacteristic* mouseChr = nullptr;
static volatile bool connected = false;
static volatile uint16_t connHandle = 0;
static uint16_t connIntervalUnits = 0, connLatency = 0, connTimeoutUnits = 0, connMtu = 0;
static uint32_t paramReqAt = 0;
static uint32_t gateUntil = 0;   // touch gate after boot/reconnect/refresh

// ---------------- telemetry (from s11) ----------------
static volatile uint32_t tFrames = 0, tNotifies = 0, tNotifyFail = 0;
static uint32_t lastFrameUs = 0, gapMinUs = 0xFFFFFFFF, gapMaxUs = 0, gapSumUs = 0;
static const uint32_t BUCKET_EDGES_US[7] = {4000,6000,8000,10000,12000,16000,20000};
static uint32_t gapBuckets[8] = {};
static uint32_t watchUntil = 0, winStart = 0, winFrames = 0, winNotifies = 0;

static void noteFrameGap(uint32_t nowUs) {
  if (lastFrameUs) {
    uint32_t g = nowUs - lastFrameUs;
    if (g < gapMinUs) gapMinUs = g;
    if (g > gapMaxUs) gapMaxUs = g;
    gapSumUs += g;
    uint8_t b = 7;
    for (uint8_t i=0;i<7;++i) if (g < BUCKET_EDGES_US[i]) { b=i; break; }
    gapBuckets[b]++;
  }
  lastFrameUs = nowUs;
}
static void resetStats() {
  tFrames=tNotifies=tNotifyFail=0; lastFrameUs=0;
  gapMinUs=0xFFFFFFFF; gapMaxUs=gapSumUs=0;
  for (uint8_t i=0;i<8;++i) gapBuckets[i]=0;
  winStart=millis(); winFrames=winNotifies=0;
}

class Cb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& c) override {
    connected = true;
    connHandle = c.getConnHandle();
    connIntervalUnits = c.getConnInterval();
    connLatency = c.getConnLatency();
    connTimeoutUnits = c.getConnTimeout();
    connMtu = c.getMTU();
    paramReqAt = millis() + 1500;
    Serial.printf("[BLE] connected %s interval=%.2fms latency=%u\n",
                  c.getIdAddress().toString().c_str(), connIntervalUnits*1.25f, connLatency);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int r) override {
    connected = false;
    Serial.printf("[BLE] disconnected reason=%d\n", r);
    NimBLEDevice::getAdvertising()->start();
  }
  void onConnParamsUpdate(NimBLEConnInfo& c) override {
    connIntervalUnits = c.getConnInterval();
    connLatency = c.getConnLatency();
    connTimeoutUnits = c.getConnTimeout();
    connMtu = c.getMTU();
    Serial.printf("[BLE] params interval=%.2fms latency=%u timeout=%ums mtu=%u\n",
                  connIntervalUnits*1.25f, connLatency, connTimeoutUnits*10, connMtu);
  }
};

static void bleBegin() {
  NimBLEDevice::init("Sticky Trackpad");
  NimBLEDevice::setSecurityAuth(true,false,false);           // bonding, Just Works
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new Cb());
  server->advertiseOnDisconnect(true);
  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Seeed");
  hid->setHidInfo(0x00, 0x03);
  hid->setPnp(0x02, 0xFFFF, 0x5354, 0x0001);
  hid->setReportMap(REPORT_MAP, sizeof(REPORT_MAP));
  mouseChr = hid->getInputReport(0);
  hid->setBatteryLevel(100);
  server->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID("1812"); adv->setAppearance(0x03C0); adv->setName("Sticky Trackpad");
  adv->start();
  Serial.printf("[BLE] advertising \"Sticky Trackpad\" %s bonds=%d\n",
                NimBLEDevice::getAddress().toString().c_str(), NimBLEDevice::getNumBonds());
}

static void forgetHost() {
  int n = NimBLEDevice::getNumBonds();
  NimBLEDevice::deleteAllBonds();
  if (connected) NimBLEDevice::getServer()->disconnect(connHandle);
  NimBLEDevice::getAdvertising()->start();
  Serial.printf("[BLE] forgot %d bond(s), advertising for a new host\n", n);
}

// ---------------- HID out ----------------
static uint8_t buttons = 0;
static void sendMouse(uint8_t b, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  buttons = b;
  if (!connected || !mouseChr) return;
  uint8_t r[5] = { b, (uint8_t)x, (uint8_t)y, (uint8_t)wheel, (uint8_t)pan };
  mouseChr->setValue(r, 5);
  if (mouseChr->notify(r, 5)) ++tNotifies; else ++tNotifyFail;
}
static void click(uint8_t mask) {
  sendMouse(buttons|mask, 0,0,0,0);
  Serial.printf("[CLICK t=%lu mask=%u]\n", millis(), mask);
  delay(15);
  sendMouse(buttons&~mask, 0,0,0,0);
}

static stickyapp::GestureEngine engine;

static void runAction(const stickyapp::Actions& a) {
  for (uint8_t i=0;i<a.count;++i) {
    auto& act = a.items[i];
    switch (act.type) {
      case stickyapp::Move:    sendMouse(buttons, act.a, act.b, 0, 0); break;
      // engine: b = vertical (positive follows finger down), a = horizontal
      case stickyapp::Scroll:  sendMouse(buttons, 0, 0, act.b, act.a); break;
      case stickyapp::Click:   click(act.a); break;
      case stickyapp::Press:   sendMouse(buttons|act.a, 0,0,0,0); break;
      case stickyapp::Release: sendMouse(buttons&~act.a, 0,0,0,0); break;
      case stickyapp::Zoom:    break;   // mouse-only MVP: pinch is ignored
      default: break;                   // 3F swipes: no keyboard in MVP
    }
  }
}

static void applyTuning() {
  engine.setTuning(tuning);
  engine.setDimensions(rot.width(), rot.height());
}

// ---------------- drawing in rotated screen space ----------------
static bool displayOn = false;
static inline void px(int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= rot.width() || y >= rot.height()) return;
  int p, q; rot.toPortrait(x, y, p, q);
  stickyDrawPixel(p, q, black);
}
static void fill(int x, int y, int w, int h, bool black) {
  for (int yy=y; yy<y+h; ++yy) for (int xx=x; xx<x+w; ++xx) px(xx, yy, black);
}
static void frame(int x, int y, int w, int h, int t) {
  fill(x, y, w, t, true); fill(x, y+h-t, w, t, true);
  fill(x, y, t, h, true); fill(x+w-t, y, t, h, true);
}
static void dot(int cx, int cy, int r, bool black) {
  for (int y=-r; y<=r; ++y) for (int x=-r; x<=r; ++x) if (x*x+y*y <= r*r) px(cx+x, cy+y, black);
}
static void text(int x, int y, const char* s, int scale, bool black) {
  for (; *s; ++s, x += 6*scale) {
    const uint8_t* rows = glyphLookup(*s);
    for (int yy=0; yy<7; ++yy) for (int xx=0; xx<5; ++xx)
      if (rows[yy] & (16 >> xx)) fill(x + xx*scale, y + yy*scale, scale, scale, black);
  }
}
static void textC(int cx, int y, const char* s, int scale, bool black) {
  text(cx - (int(strlen(s))*6 - 1)*scale/2, y, s, scale, black);
}
static void statusDot(int cx, int cy) {
  dot(cx, cy, 7, true);
  if (!connected) dot(cx, cy, 4, false);
}

static stickymvp::Screen screen = SCR_TRACKPAD;
static stickymvp::TapDetector tap;
static stickymvp::ForgetConfirm forgetConfirm;

static void drawTrackpad() {
  const int W = rot.width(), H = rot.height(), E = int(tuning.edgeWidthPx);
  for (int y=40; y<H-10; y+=40)
    for (int x=E+20; x<W-E-10; x+=40) dot(x, y, 1, true);
  for (int y=60; y<H-40; y+=14) {                 // dashed strip borders
    fill(E, y, 2, 7, true);
    fill(W-E-2, y, 2, 7, true);
  }
  for (int s=0; s<2; ++s) {                       // small up/down chevrons in strips
    int cx = s ? W - E/2 : E/2;
    for (int i=0; i<6; ++i) { px(cx-i, 40+i, true); px(cx+i, 40+i, true);
                              px(cx-i, H-30-i, true); px(cx+i, H-30-i, true); }
  }
  statusDot(W/2, 16);
}

static void drawSettings() {
  const int W = rot.width(), H = rot.height();
  SettingsLayout L(W, H);
  textC(W/2, 28, "SETTINGS", 4, true);
  statusDot(40, 100);
  text(60, 93, connected ? "CONNECTED" : "PAIRING / WAITING", 2, true);
  char line[40];
  snprintf(line, sizeof line, "BONDS %d   ROT %d", NimBLEDevice::getNumBonds(), rot.r * 90);
  text(60, 120, line, 2, true);
  const char* labels[3] = { nullptr, nullptr, "BACK" };
  char rl[20]; snprintf(rl, sizeof rl, "ROTATE  %d", rot.r * 90);
  labels[0] = rl;
  labels[1] = forgetConfirm.armed() ? "TAP AGAIN TO FORGET" : "FORGET HOST";
  for (int i=0; i<stickymvp::BtnCount; ++i) {
    const auto& b = L.btn[i];
    bool inverted = (i == stickymvp::BtnForget && forgetConfirm.armed());
    if (inverted) fill(b.x, b.y, b.w, b.h, true); else frame(b.x, b.y, b.w, b.h, 3);
    int scale = (int(strlen(labels[i]))*6*3 < b.w - 20) ? 3 : 2;
    textC(b.x + b.w/2, b.y + (b.h - 7*scale)/2, labels[i], scale, !inverted);
  }
  (void)H;
}

static void redraw() {
  if (!displayOn) return;
  stickyDisplayClear();
  if (screen == SCR_TRACKPAD) drawTrackpad(); else drawSettings();
  uint32_t t0 = millis();
  stickyDisplayRefresh();
  Serial.printf("[UI] %s rot=%d refresh=%lums\n", screen == SCR_TRACKPAD ? "TRACKPAD" : "SETTINGS",
                rot.r * 90, millis() - t0);
  // The refresh blocked touch reading: drop any stale state and gate phantoms.
  runAction(engine.cancel());
  tap.reset();
  gateUntil = millis() + 250;
}

static void setScreen(Screen s) {
  runAction(engine.cancel());
  forgetConfirm.cancel();
  screen = s;
  redraw();
}

static void settingsTap(int x, int y) {
  SettingsLayout L(rot.width(), rot.height());
  switch (L.hit(x, y)) {
    case stickymvp::BtnRotate:
      rot.r = (rot.r + 1) & 3; saveSettings(); applyTuning(); forgetConfirm.cancel(); redraw(); break;
    case stickymvp::BtnForget:
      if (forgetConfirm.press(millis())) forgetHost();
      redraw(); break;
    case stickymvp::BtnBack:
      setScreen(SCR_TRACKPAD); break;
    default: break;
  }
}

// ---------------- buttons (active-low) ----------------
static const int BTN_PINS[3] = {4, 5, 6};  // POWER/OK, UP, DOWN
static int btnLast[3] = {HIGH, HIGH, HIGH};
static uint32_t btnChange[3] = {}, btnDown[3] = {};
static void handleButtons() {
  uint32_t now = millis();
  for (int i=0; i<3; ++i) {
    int raw = digitalRead(BTN_PINS[i]);
    if (raw == btnLast[i] || now - btnChange[i] < 25) continue;
    btnLast[i] = raw; btnChange[i] = now;
    if (raw == LOW) { btnDown[i] = now; continue; }
    if (now - btnDown[i] < 1000)                // short press only; long GPIO4 = PMIC
      setScreen(screen == SCR_TRACKPAD ? SCR_SETTINGS : SCR_TRACKPAD);
  }
}

// ---------------- serial ----------------
static void printStats() {
  uint32_t f = tFrames, gaps = f ? f-1 : 0;
  Serial.printf("STATS frames=%lu notifies=%lu notify_fail=%lu\n", f, (uint32_t)tNotifies, (uint32_t)tNotifyFail);
  if (gaps) {
    Serial.printf("GAP us min=%lu avg=%lu max=%lu\n", gapMinUs, gapSumUs/gaps, gapMaxUs);
    Serial.printf("HIST <4ms=%lu 4-6=%lu 6-8=%lu 8-10=%lu 10-12=%lu 12-16=%lu 16-20=%lu >20ms=%lu\n",
                  gapBuckets[0],gapBuckets[1],gapBuckets[2],gapBuckets[3],
                  gapBuckets[4],gapBuckets[5],gapBuckets[6],gapBuckets[7]);
  }
  Serial.printf("CONN interval=%.2fms latency=%u timeout=%ums mtu=%u connected=%u\n",
                connIntervalUnits*1.25f, connLatency, connTimeoutUnits*10, connMtu, connected);
}
static void handleSerial() {
  while (Serial.available()) {
    String c = Serial.readStringUntil('\n'); c.trim();
    if (c=="PING") Serial.println("PONG s12_mvp");
    else if (c=="STATUS") Serial.printf("connected=%u bonds=%d screen=%d rot=%d edge=%.0f sign=%.0f\n",
                                        connected, NimBLEDevice::getNumBonds(), int(screen), rot.r*90,
                                        tuning.edgeWidthPx, tuning.sideStripSign);
    else if (c=="STATS") printStats();
    else if (c=="RESET-STATS") { resetStats(); Serial.println("STATS RESET"); }
    else if (c.startsWith("WATCH")) {
      int n = c.substring(5).toInt(); if (n<=0) n=10;
      watchUntil = millis() + uint32_t(n)*1000; winStart = millis(); winFrames = winNotifies = 0;
      Serial.printf("WATCH %ds\n", n);
    }
    else if (c=="FORGET") { forgetHost(); redraw(); }
    else if (c.startsWith("ROTATE")) {
      int d = c.substring(6).toInt();
      rot.r = uint8_t((d / 90) & 3); saveSettings(); applyTuning(); redraw();
    }
    else if (c.startsWith("EDGE")) {
      int e = c.substring(4).toInt();
      if (e >= 0 && e <= 120) { tuning.edgeWidthPx = e; saveSettings(); applyTuning(); redraw(); }
      Serial.printf("edge=%.0f\n", tuning.edgeWidthPx);
    }
    else if (c.startsWith("SIGN")) {
      tuning.sideStripSign = c.substring(4).toInt() < 0 ? -1.0f : 1.0f;
      saveSettings(); applyTuning(); Serial.printf("sign=%.0f\n", tuning.sideStripSign);
    }
    else if (c=="SCREEN T") setScreen(SCR_TRACKPAD);
    else if (c=="SCREEN S") setScreen(SCR_SETTINGS);
    else if (c.length()) Serial.printf("? %s\n", c.c_str());
  }
}

// ---------------- setup / loop ----------------
static bool wasConnected = false;

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);   // board power hold
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  for (int p : BTN_PINS) pinMode(p, INPUT_PULLUP);
  Serial.begin(115200); delay(800);
  Serial.println("S12 MVP — trackpad + settings + rotation + side scroll strips");
  loadSettings(); applyTuning();
  Serial.printf("Touch(400kHz): %s\n", stickyTouchBegin(400000) ? "OK" : "FAILED");
  displayOn = stickyDisplayBegin();
  Serial.printf("Display: %s\n", displayOn ? "OK" : "FAILED");
  resetStats();
  bleBegin();
  redraw();
  Serial.println("READY");
}

void loop() {
  handleSerial();
  handleButtons();

  if (paramReqAt && int32_t(millis() - paramReqAt) >= 0) {
    paramReqAt = 0;
    if (connected && connIntervalUnits > 12) {
      NimBLEDevice::getServer()->updateConnParams(connHandle, 6, 12, 0, 400);
      Serial.println("[BLE] requested interval 7.5-15ms");
    }
  }

  if (watchUntil && int32_t(millis() - watchUntil) >= 0) { watchUntil = 0; Serial.println("WATCH END"); printStats(); }
  else if (watchUntil && int32_t(millis() - winStart) >= 1000) {
    uint32_t dt = millis() - winStart;
    Serial.printf("RATE touch_hz=%.1f notify_hz=%.1f\n", winFrames*1000.0f/dt, winNotifies*1000.0f/dt);
    winStart = millis(); winFrames = winNotifies = 0;
  }

  if (connected != wasConnected) {           // status changes are the only async redraws
    wasConnected = connected;
    if (!connected) { runAction(engine.cancel()); buttons = 0; }
    redraw();
  }
  if (screen == SCR_SETTINGS && forgetConfirm.expired(millis())) redraw();

  if (gateUntil && int32_t(millis() - gateUntil) < 0) { stickyTouchRead(nullptr, 0); delay(2); return; }

  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  if (n < 0) { delay(2); return; }
  ++tFrames; ++winFrames;
  noteFrameGap(micros());

  stickyapp::Contact contacts[5];
  uint8_t count = n > 5 ? 5 : uint8_t(n);
  for (uint8_t i=0; i<count; ++i) {
    int sx, sy;
    rot.toScreen(pts[i].x > 479 ? 479 : pts[i].x, pts[i].y > 799 ? 799 : pts[i].y, sx, sy);
    contacts[i] = { pts[i].id, float(sx), float(sy) };
  }

  if (screen == SCR_TRACKPAD) {
    if (connected) {
      uint32_t before = tNotifies;
      // >3 contacts: engine treats it as invalid and suppresses until all-up.
      runAction(engine.update(contacts, count, millis()));
      winNotifies += tNotifies - before;
    }
  } else {
    int tx, ty;
    if (tap.update(count, count ? int(contacts[0].x) : 0, count ? int(contacts[0].y) : 0, millis(), tx, ty))
      settingsTap(tx, ty);
  }
  delay(2);
}
