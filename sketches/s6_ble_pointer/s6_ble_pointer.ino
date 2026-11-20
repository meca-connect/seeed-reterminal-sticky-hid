/*
 * s6_ble_pointer — minimal BLE HID mouse on the verified portrait model.
 *
 * First BLE smoke test: one finger moves the pointer (relative, subpixel +
 * acceleration), tap = left click. No display, no gestures yet. Uses the
 * portrait touch model from sketches/_common/sticky_hw.h (raw == logical).
 *
 * Pair from a phone/laptop: it advertises "Sticky Trackpad" (HID mouse).
 * Serial PING/STATUS for diagnostics. Stock USB-C is UART-only; BLE is the
 * standalone HID transport (USB HID is impossible on this hardware).
 */
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "../_common/sticky_hw.h"

static uint8_t REPORT_MAP[] = {
  0x05,0x01, 0x09,0x02, 0xA1,0x01,   // Usage Page (Generic Desktop), Mouse, Collection
  0x09,0x01, 0xA1,0x00,               // Pointer, Collection
  0x05,0x09, 0x19,0x01, 0x29,0x03,   // Buttons 1..3
  0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
  0x95,0x01, 0x75,0x05, 0x81,0x03,   // 5 bits padding
  0x05,0x01, 0x09,0x30, 0x09,0x31,   // X, Y
  0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x02, 0x81,0x06,
  0x09,0x38,                          // Wheel
  0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01, 0x81,0x06,
  0xC0, 0xC0
};

static NimBLEHIDDevice* hid = nullptr;
static NimBLECharacteristic* mouseChr = nullptr;
static volatile bool connected = false;

class Callbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& c) override { connected = true; Serial.printf("[BLE] connected peer=%s\n", c.getIdAddress().toString().c_str()); }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& c, int r) override { connected = false; Serial.printf("[BLE] disconnected reason=%d\n", r); NimBLEDevice::getAdvertising()->start(); }
};

static void bleBegin() {
  NimBLEDevice::init("Sticky Trackpad");
  NimBLEDevice::setSecurityAuth(true,false,false);          // bonding, Just Works
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new Callbacks());
  server->advertiseOnDisconnect(true);
  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("Seeed");
  hid->setHidInfo(0x00, 0x03);
  hid->setPnp(0x02, 0xFFFF, 0x5354, 0x0001);               // research IDs
  hid->setReportMap(REPORT_MAP, sizeof(REPORT_MAP));
  mouseChr = hid->getInputReport(0);
  hid->setBatteryLevel(100);
  server->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID("1812");
  adv->setAppearance(0x03C0);
  adv->setName("Sticky Trackpad");
  adv->start();
  Serial.printf("[BLE] advertising as \"Sticky Trackpad\" addr=%s\n", NimBLEDevice::getAddress().toString().c_str());
}

static void sendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
  if (!connected || !mouseChr) return;
  uint8_t r[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel };
  mouseChr->setValue(r, 4);
  mouseChr->notify(r, 4);
}

// ---- 1-finger pointer + tap ----------------------------------------------
static const uint32_t TAP_MAX_MS = 220;
static const float TAP_MAX_DIST = 20.0f;
static const float GAIN = 1.2f;
static bool down = false;
static uint32_t downMs = 0;
static float startX=0, startY=0, lastX=0, lastY=0, resX=0, resY=0, travel=0;

static void pointerFrame(StickyTouchPoint& p, uint32_t now) {
  if (!down) {
    down = true; downMs = now; startX = lastX = p.x; startY = lastY = p.y; travel = 0; resX = resY = 0;
    return;
  }
  float dx = p.x - lastX, dy = p.y - lastY;
  float fromStart = sqrtf((p.x-startX)*(p.x-startX)+(p.y-startY)*(p.y-startY));
  if (fromStart > travel) travel = fromStart;
  lastX = p.x; lastY = p.y;
  if (fabsf(dx) > 120 || fabsf(dy) > 120) { resX = resY = 0; return; } // jump guard
  resX += dx * GAIN;
  resY += dy * GAIN;   // logical y is already top->bottom (header flips raw y)
  int8_t mx = 0, my = 0;
  while (resX >= 1.0f) { mx++; resX -= 1.0f; }
  while (resX <= -1.0f) { mx--; resX += 1.0f; }
  while (resY >= 1.0f) { my++; resY -= 1.0f; }
  while (resY <= -1.0f) { my--; resY += 1.0f; }
  if (mx || my) sendMouse(0, mx, my, 0);
}

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S6 BLE POINTER — 1 finger = pointer, tap = left click");
  Serial.printf("Touch: %s\n", stickyTouchBegin() ? "OK" : "FAILED");
  bleBegin();
  Serial.println("READY — pair \"Sticky Trackpad\" from your phone/laptop");
}

void loop() {
  while (Serial.available()) {
    String c = Serial.readStringUntil('\n'); c.trim();
    if (c=="PING") Serial.println("PONG s6_ble_pointer");
    else if (c=="STATUS") Serial.printf("connected=%u addr=%s\n", connected, NimBLEDevice::getAddress().toString().c_str());
  }
  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  uint32_t now = millis();
  if (n == 1) {
    pointerFrame(pts[0], now);
  } else if (n == 0 && down) {
    down = false;
    uint32_t held = now - downMs;
    if (held <= TAP_MAX_MS && travel <= TAP_MAX_DIST) {   // tap -> left click
      sendMouse(1, 0, 0, 0); delay(12); sendMouse(0, 0, 0, 0);
      Serial.printf("[CLICK] tap %lums travel=%.0f\n", (unsigned long)held, travel);
    }
  }
  delay(5);
}
