/*
 * s10_rate_probe — measurement build for the MVP design process (docs/DESIGN.md P0).
 *
 * IDENTICAL gesture/transport path to s7_trackpad (mouse-only, GestureEngine,
 * NimBLE HID notify) plus bounded telemetry so we can answer R1/R4 with data:
 *   - GT911 fresh-frame interval histogram (true sensor frame rate)
 *   - successful BLE notification counter + per-second rate window
 *   - negotiated connection interval / latency / supervision timeout / MTU
 *
 * Serial commands: PING, STATUS, STATS, RESET-STATS, WATCH <n>, FORGET.
 * WATCH n prints one RATE line per second for n seconds (default output is
 * silent during motion so serial work never perturbs the input path).
 *
 * Deviation from s7 (deliberate, probe-only): loop polls with delay(2) instead
 * of delay(5) so polling (~300-400 Hz) is faster than the sensor frame rate
 * and the measured interval histogram reflects the GT911, not our loop.
 *
 * Stock USB-C is UART-only; BLE is the HID path. No touch config writes.
 */
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "../_common/sticky_hw.h"
#include "../../sticky_trackpad/GestureEngine.h"

// Mouse report: buttons(1) x(1) y(1) wheel(1) pan(1)
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

// ---------------- telemetry ----------------
static volatile uint32_t tFrames = 0;        // fresh GT911 frames observed
static volatile uint32_t tNotifies = 0;      // successful mouse notifications
static volatile uint32_t tNotifyFail = 0;
static uint32_t lastFrameUs = 0;             // micros() of previous fresh frame
static uint32_t gapMinUs = 0xFFFFFFFF, gapMaxUs = 0, gapSumUs = 0;
static const uint32_t BUCKET_EDGES_US[7] = {4000,6000,8000,10000,12000,16000,20000};
static uint32_t gapBuckets[8] = {};          // <4,4-6,6-8,8-10,10-12,12-16,16-20,>20 ms
static uint16_t connIntervalUnits = 0, connLatency = 0, connTimeoutUnits = 0, connMtu = 0;
static volatile uint16_t connHandle = 0;
static uint32_t watchUntil = 0;              // millis(); 0 = silent
static uint32_t winStart = 0, winFrames = 0, winNotifies = 0;

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
    Serial.printf("[BLE] connected %s interval=%.2fms latency=%u timeout=%ums mtu=%u\n",
                  c.getIdAddress().toString().c_str(), connIntervalUnits*1.25f,
                  connLatency, connTimeoutUnits*10, connMtu);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int r) override {
    connected = false;
    Serial.printf("[BLE] disconnected reason=%d\n", r);
    NimBLEDevice::getAdvertising()->start();
  }
};

static void bleBegin() {
  NimBLEDevice::init("Sticky Trackpad");
  NimBLEDevice::setSecurityAuth(true,false,false);
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
  Serial.printf("[BLE] advertising \"Sticky Trackpad\" %s\n", NimBLEDevice::getAddress().toString().c_str());
}

static uint8_t buttons = 0;
static void sendMouse(uint8_t b, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  if (!connected || !mouseChr) return;
  buttons = b;
  uint8_t r[5] = { b, (uint8_t)x, (uint8_t)y, (uint8_t)wheel, (uint8_t)pan };
  mouseChr->setValue(r, 5);
  if (mouseChr->notify(r, 5)) ++tNotifies; else ++tNotifyFail;
}
static void click(uint8_t mask) { sendMouse(buttons|mask, 0,0,0,0); Serial.printf("[CLICK t=%lu mask=%u]\n", millis(), mask); delay(15); sendMouse(buttons&~mask, 0,0,0,0); }
static void press(uint8_t mask) { sendMouse(buttons|mask, 0,0,0,0); }
static void release(uint8_t mask) { sendMouse(buttons&~mask, 0,0,0,0); }

static stickyapp::GestureEngine engine;

static void runAction(const stickyapp::Actions& a) {
  for (uint8_t i=0;i<a.count;++i) {
    auto& act = a.items[i];
    switch (act.type) {
      case stickyapp::Move:   sendMouse(buttons, act.a, act.b, 0, 0); break;
      case stickyapp::Scroll: sendMouse(buttons, 0, 0, act.b, act.a); break; // b=vertical(wheel), a=horizontal(pan)
      case stickyapp::Click:  click(act.a); break;
      case stickyapp::Press:  press(act.a); break;
      case stickyapp::Release:release(act.a); break;
      case stickyapp::Zoom:   sendMouse(buttons, 0, 0, act.a, 0); break;
      default: Serial.printf("[SWIPE] type=%u (logged only)\n", unsigned(act.type)); break;
    }
  }
}

static void printStats() {
  uint32_t f = tFrames, n = tNotifies, nf = tNotifyFail;
  uint32_t gaps = f ? f-1 : 0;
  Serial.printf("STATS frames=%lu notifies=%lu notify_fail=%lu\n", f, n, nf);
  if (gaps) {
    Serial.printf("GAP us min=%lu avg=%lu max=%lu\n", gapMinUs, gapSumUs/gaps, gapMaxUs);
    Serial.printf("HIST <4ms=%lu 4-6=%lu 6-8=%lu 8-10=%lu 10-12=%lu 12-16=%lu 16-20=%lu >20ms=%lu\n",
                  gapBuckets[0],gapBuckets[1],gapBuckets[2],gapBuckets[3],
                  gapBuckets[4],gapBuckets[5],gapBuckets[6],gapBuckets[7]);
  }
  Serial.printf("CONN interval=%.2fms latency=%u timeout=%ums mtu=%u connected=%u\n",
                connIntervalUnits*1.25f, connLatency, connTimeoutUnits*10, connMtu, connected);
}

static bool wasConnected = false;

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S10 RATE PROBE — s7 gesture path + telemetry (PING/STATUS/STATS/RESET-STATS/WATCH n/FORGET)");
  Serial.printf("Touch: %s\n", stickyTouchBegin() ? "OK" : "FAILED");
  resetStats();
  bleBegin();
  Serial.println("READY");
}

void loop() {
  while (Serial.available()) {
    String c = Serial.readStringUntil('\n'); c.trim();
    if (c=="PING") Serial.println("PONG s10_rate_probe");
    else if (c=="STATUS") Serial.printf("connected=%u buttons=0x%02X addr=%s\n", connected, buttons, NimBLEDevice::getAddress().toString().c_str());
    else if (c=="STATS") printStats();
    else if (c=="RESET-STATS") { resetStats(); Serial.println("STATS RESET"); }
    else if (c.startsWith("WATCH")) {
      int n = c.substring(5).toInt(); if (n<=0) n=10;
      watchUntil = millis() + uint32_t(n)*1000;
      winStart = millis(); winFrames = winNotifies = 0;
      Serial.printf("WATCH %ds\n", n);
    }
    else if (c=="FORGET") {
      Serial.printf("forget: bonds=%d deleted\n", NimBLEDevice::deleteAllBonds());
      if (connected) NimBLEDevice::getServer()->disconnect(connHandle);
      NimBLEDevice::getAdvertising()->start();
    }
  }

  // 1 Hz RATE line, only inside an explicit WATCH window
  if (watchUntil && int32_t(millis() - watchUntil) >= 0) { watchUntil = 0; Serial.println("WATCH END"); printStats(); }
  else if (watchUntil && int32_t(millis() - winStart) >= 1000) {
    uint32_t dt = millis() - winStart;
    Serial.printf("RATE touch_hz=%.1f notify_hz=%.1f frames=%lu notifies=%lu\n",
                  winFrames*1000.0f/dt, winNotifies*1000.0f/dt, (uint32_t)tFrames, (uint32_t)tNotifies);
    winStart = millis(); winFrames = winNotifies = 0;
  }

  if (connected != wasConnected) {
    wasConnected = connected;
    if (!connected) { engine.cancel(); buttons = 0; }
  }

  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  if (n < 0) { delay(2); return; }

  // telemetry: fresh frame observed
  ++tFrames; ++winFrames;
  noteFrameGap(micros());

  if (connected) {
    stickyapp::Contact contacts[3];
    uint8_t count = n > 3 ? 3 : n;         // engine caps at 3; >3 suppresses
    for (uint8_t i=0;i<count;++i) { contacts[i].id = pts[i].id; contacts[i].x = pts[i].x; contacts[i].y = pts[i].y; }
    uint32_t before = tNotifies;
    runAction(engine.update(contacts, count, millis()));
    winNotifies += tNotifies - before;
  }
  delay(2);
}
