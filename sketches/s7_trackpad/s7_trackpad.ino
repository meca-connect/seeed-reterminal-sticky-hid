/*
 * s7_trackpad — BLE multitouch trackpad (PaperS3 gesture set ported to Sticky).
 *
 * Uses the verified portrait driver (sketches/_common/sticky_hw.h) and the
 * host-tested GestureEngine (sticky_trackpad/GestureEngine.h, retuned to
 * 480x800). Gestures:
 *   1 finger: pointer (relative) + tap = left click; long-press/double-tap = drag
 *   2 fingers: scroll (vertical + horizontal) + tap = right click; pinch = zoom
 *   3 fingers: tap = middle click; swipe = shortcuts (logged, keyboard not wired yet)
 *   right edge (x>=460): one-finger scroll strip
 * No display. Serial PING/STATUS. Stock USB-C is UART-only; BLE is the HID path.
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

class Cb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& c) override { connected = true; Serial.printf("[BLE] connected %s\n", c.getIdAddress().toString().c_str()); }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int r) override { connected = false; Serial.printf("[BLE] disconnected %d\n", r); NimBLEDevice::getAdvertising()->start(); }
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
  mouseChr->notify(r, 5);
}
static void click(uint8_t mask) { sendMouse(buttons|mask, 0,0,0,0); delay(15); sendMouse(buttons&~mask, 0,0,0,0); }
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
      case stickyapp::Zoom:   sendMouse(buttons, 0, 0, act.a, 0); break; // wheel without Ctrl (no keyboard yet)
      default: Serial.printf("[SWIPE] type=%u (keyboard not wired)\n", unsigned(act.type)); break;
    }
  }
}

static bool wasConnected = false;

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S7 TRACKPAD — 1F pointer, 2F scroll, 3F middle; tap=click, long-press=drag");
  Serial.printf("Touch: %s\n", stickyTouchBegin() ? "OK" : "FAILED");
  bleBegin();
  Serial.println("READY");
}

void loop() {
  while (Serial.available()) {
    String c = Serial.readStringUntil('\n'); c.trim();
    if (c=="PING") Serial.println("PONG s7_trackpad");
    else if (c=="STATUS") Serial.printf("connected=%u buttons=0x%02X addr=%s\n", connected, buttons, NimBLEDevice::getAddress().toString().c_str());
  }

  if (connected != wasConnected) {
    wasConnected = connected;
    if (!connected) { engine.cancel(); buttons = 0; }
  }

  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  if (n < 0) { delay(5); return; }

  if (connected) {
    stickyapp::Contact contacts[3];
    uint8_t count = n > 3 ? 3 : n;         // engine caps at 3; >3 suppresses
    for (uint8_t i=0;i<count;++i) { contacts[i].id = pts[i].id; contacts[i].x = pts[i].x; contacts[i].y = pts[i].y; }
    runAction(engine.update(contacts, count, millis()));
  }
  delay(5);
}
