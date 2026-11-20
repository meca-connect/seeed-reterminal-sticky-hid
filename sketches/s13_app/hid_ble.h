#pragma once
// BLE HID: mouse (report 1) + keyboard (report 2) + battery service.
// Link tuning: 1.5 s after connect we ask for a 7.5-15 ms interval WITH slave
// latency. Latency only lets the Sticky skip listening when it has nothing to
// send - outgoing input still goes out at the next connection event - so it
// saves radio power without adding input lag. If the host refuses (interval
// stays > 15 ms) we retry once with latency 0 (the s11/s12-proven request).
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace hid {
static uint8_t REPORT_MAP[] = {
  // Mouse, report 1: buttons(3) x y wheel pan
  0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,0x01, 0x09,0x01, 0xA1,0x00,
  0x05,0x09, 0x19,0x01, 0x29,0x03, 0x15,0x00, 0x25,0x01, 0x95,0x03, 0x75,0x01, 0x81,0x02,
  0x95,0x01, 0x75,0x05, 0x81,0x03,
  0x05,0x01, 0x09,0x30, 0x09,0x31, 0x09,0x38, 0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x03, 0x81,0x06,
  0x05,0x0C, 0x0A,0x38,0x02, 0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01, 0x81,0x06,
  0xC0, 0xC0,
  // Keyboard, report 2: modifiers, reserved, LEDs (out), 6 keys
  0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x02,
  0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
  0x95,0x01, 0x75,0x08, 0x81,0x03,
  0x95,0x05, 0x75,0x01, 0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02,
  0x95,0x01, 0x75,0x03, 0x91,0x03,
  0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65, 0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00,
  0xC0
};
static const uint8_t MAP_VERSION = 2;   // bump when REPORT_MAP changes

static NimBLEServer* server = nullptr;
static NimBLEHIDDevice* dev = nullptr;
static NimBLECharacteristic *mouseIn = nullptr, *kbIn = nullptr;
static volatile bool connected = false;
static volatile uint16_t handle = 0;
static volatile uint16_t intervalUnits = 0, latency = 0;
static uint32_t tuneAt = 0;
static uint8_t tuneStage = 0;     // 0 none, 1 asked with latency, 2 asked without
static uint16_t wantLatency = 8;
static uint8_t buttons = 0;
static uint32_t notifies = 0, notifyFail = 0;

struct Cb : NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& c) override {
    connected = true; handle = c.getConnHandle();
    intervalUnits = c.getConnInterval(); latency = c.getConnLatency();
    tuneAt = millis() + 1500; tuneStage = 0; buttons = 0;
    Serial.printf("[BLE] connected %s interval=%.2fms latency=%u\n",
                  c.getIdAddress().toString().c_str(), intervalUnits * 1.25f, latency);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int r) override {
    connected = false; buttons = 0;
    Serial.printf("[BLE] disconnected reason=%d\n", r);
    NimBLEDevice::getAdvertising()->start();
  }
  void onConnParamsUpdate(NimBLEConnInfo& c) override {
    intervalUnits = c.getConnInterval(); latency = c.getConnLatency();
    Serial.printf("[BLE] params interval=%.2fms latency=%u timeout=%ums\n",
                  intervalUnits * 1.25f, latency, c.getConnTimeout() * 10);
  }
};

static void begin(const char* name) {
  NimBLEDevice::init(name);
  NimBLEDevice::setPower(3);                         // dBm; desk range, less current
  NimBLEDevice::setSecurityAuth(true, false, false); // bonding, Just Works
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  server = NimBLEDevice::createServer();
  server->setCallbacks(new Cb());
  server->advertiseOnDisconnect(true);
  dev = new NimBLEHIDDevice(server);
  dev->setManufacturer("Seeed");
  dev->setHidInfo(0x00, 0x03);
  dev->setPnp(0x02, 0xFFFF, 0x5354, 0x0002);
  dev->setReportMap(REPORT_MAP, sizeof(REPORT_MAP));
  mouseIn = dev->getInputReport(1);
  kbIn = dev->getInputReport(2);
  dev->getOutputReport(2);                           // LED report hosts expect
  dev->setBatteryLevel(100);
  server->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID("1812"); adv->setAppearance(0x03C0); adv->setName(name);
  adv->setPreferredParams(6, 12);
  adv->start();
  Serial.printf("[BLE] advertising \"%s\" bonds=%d\n", name, NimBLEDevice::getNumBonds());
}
static int bonds() { int n = NimBLEDevice::getNumBonds(); return n < 0 ? 0 : n; }
static bool pairing() { return !connected && bonds() == 0; }

static void service() {
  if (!connected || !tuneAt || int32_t(millis() - tuneAt) < 0) return;
  if (tuneStage == 0) {
    server->updateConnParams(handle, 6, 12, wantLatency, 400);
    Serial.printf("[BLE] requested 7.5-15ms latency=%u\n", wantLatency);
    tuneStage = 1; tuneAt = millis() + 3000;
  } else if (tuneStage == 1) {
    if (intervalUnits > 12) {
      server->updateConnParams(handle, 6, 12, 0, 400);
      Serial.println("[BLE] host refused latency; requested 7.5-15ms latency=0");
    }
    tuneStage = 2; tuneAt = 0;
  } else tuneAt = 0;
}

static void mouse(uint8_t b, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  buttons = b;
  if (!connected || !mouseIn) return;
  uint8_t r[5] = {b, uint8_t(x), uint8_t(y), uint8_t(wheel), uint8_t(pan)};
  mouseIn->setValue(r, 5);
  if (mouseIn->notify(r, 5)) ++notifies; else ++notifyFail;
}
static void press(uint8_t m)   { mouse(buttons | m, 0, 0, 0, 0); }
static void release(uint8_t m) { mouse(buttons & ~m, 0, 0, 0, 0); }
static void click(uint8_t m)   { press(m); delay(12); release(m); }

static void keyReport(uint8_t mods, uint8_t usage) {
  if (!connected || !kbIn) return;
  uint8_t r[8] = {mods, 0, usage, 0, 0, 0, 0, 0};
  kbIn->setValue(r, 8);
  kbIn->notify(r, 8);
}
static void tap(uint8_t usage, uint8_t mods = 0) { keyReport(mods, usage); delay(10); keyReport(0, 0); }

static void battery(int pct) { if (dev && pct >= 0) dev->setBatteryLevel(uint8_t(pct), connected); }

static void forget() {
  NimBLEDevice::deleteAllBonds();
  if (connected) server->disconnect(handle);
  NimBLEDevice::getAdvertising()->start();
  Serial.println("[BLE] bonds cleared, advertising for a new host");
}
}  // namespace hid
