/*
 * s8_sticky_gui — Sticky trackpad + numpad + keyboard + settings on e-paper.
 *
 * Screen set (portrait 480x800, USB-C at bottom):
 *   TRACKPAD   1F pointer/tap, long-press drag; 2F scroll/pinch; 3F middle/swipe
 *   NUMPAD     3x4: 1-9, 0, <(backspace), OK(enter)
 *   KEYBOARD   10x5 QWERTY: shift(^), TAB, space, OK(enter), backspace(<)
 *   SETTINGS   BT state, bond count, controls help
 *
 * Controls: AI short=home, AI long=forget host + pair, UP/DOWN=cycle screens.
 * HID over BLE (mouse + keyboard, report IDs 1/2). Stock USB-C is UART-only.
 */
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "../_common/sticky_hw.h"
#include "../_common/font5x7.h"
#include "../_common/gui_keys.h"
#include "../../sticky_trackpad/GestureEngine.h"
#include "../../sticky_trackpad/BleTransport.h"

enum : int { SCR_TRACKPAD = 0, SCR_NUMPAD, SCR_KEYBOARD, SCR_SETTINGS, SCR_COUNT };
static const char* SCREEN_NAMES[SCR_COUNT] = {"TRACKPAD", "NUMPAD", "KEYBOARD", "SETTINGS"};

static BleTrackpadTransport ble;
static stickyapp::GestureEngine engine;
static int screen = SCR_TRACKPAD;
static bool shifted = false;
static bool dirty = true;
static bool wasConnected = false;
static uint32_t touchGateUntil = 0;   // ignore touch briefly after boot/reconnect

// ---- physical buttons (active-low) ----
static const int PIN_UP = 5, PIN_DOWN = 6, PIN_AI = 4;
static int lastBtn[3] = {1, 1, 1};
static uint32_t lastBtnChange[3] = {0, 0, 0};
static bool aiHeld = false;
static bool aiLongFired = false;
static uint32_t aiHeldSince = 0;

// ---- tap state for numpad/keyboard ----
static uint16_t downX = 0, downY = 0;
static uint32_t downAt = 0;
static bool downing = false, moved = false;

// ---------------------------------------------------------------- actions
static void runAction(const stickyapp::Actions& a) {
  for (uint8_t i = 0; i < a.count; ++i) {
    auto& act = a.items[i];
    switch (act.type) {
      case stickyapp::Move:   ble.mouseMove(act.a, act.b, 0, 0); break;
      case stickyapp::Scroll: ble.mouseMove(0, 0, act.b, act.a); break; // b=wheel, a=pan
      case stickyapp::Click:  ble.mouseClick(act.a); break;
      case stickyapp::Press:  ble.mousePress(act.a); break;
      case stickyapp::Release:ble.mouseRelease(act.a); break;
      case stickyapp::Zoom:   ble.mouseMove(0, 0, act.a, 0); break;
      case stickyapp::SwipeLeft:  ble.typeKey(0x50, 0x82); break; // Alt+Left (back)
      case stickyapp::SwipeRight: ble.typeKey(0x4F, 0x82); break; // Alt+Right (forward)
      default: break; // SwipeUp/SwipeDown unassigned
    }
  }
}

// ---------------------------------------------------------------- keys
static void typeKey(uint8_t usage) {
  if (usage == 0xFF) { shifted = !shifted; dirty = true; return; }
  if (shifted && usage >= 0x04 && usage <= 0x1D) ble.typeKey(usage, 0x81); // letters -> uppercase
  else ble.typeKey(usage);
  if (shifted) { shifted = false; dirty = true; }
}

static void handleKeyTap(uint16_t x, uint16_t y) {
  int cw, ch, gx, gy;
  if (screen == SCR_NUMPAD) {
    gridLayout(3, 4, &cw, &ch, &gx, &gy);
    int i = keyIndexAt(x, y, 3, 4, cw, ch, gx, gy);
    if (i >= 0) typeKey(NUMPAD[i].usage);
  } else if (screen == SCR_KEYBOARD) {
    gridLayout(10, 5, &cw, &ch, &gx, &gy);
    int i = keyIndexAt(x, y, 10, 5, cw, ch, gx, gy);
    if (i >= 0) typeKey(KEYBOARD[i].usage);
  }
}

// ---------------------------------------------------------------- render
static void drawHeader(const char* title, int idx) {
  fillRect(0, 0, 480, GUI_HEADER, true);
  drawText(10, 10, title, 3, false);
  char p[4] = {'<', char('0' + idx), '>', 0};
  drawText(480 - 10 - textWidth(p, 2), 12, p, 2, false);
}

static void drawFooter(const char* hint) {
  fillRect(0, 800 - GUI_FOOTER, 480, GUI_FOOTER, true);
  drawText(6, 800 - GUI_FOOTER + 8, hint, 1, false);
}

static void drawKeys(const Key* keys, int cols, int rows, int scale) {
  int cw, ch, gx, gy; gridLayout(cols, rows, &cw, &ch, &gx, &gy);
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c)
      drawKey(gx + c * (cw + GUI_GAP), gy + r * (ch + GUI_GAP), cw, ch, keys[r * cols + c].label, scale);
}

static void renderScreen() {
  stickyDisplayClear();
  const char* hint = "AI:HOME  UP/DN:PAGE";
  switch (screen) {
    case SCR_TRACKPAD:
      drawHeader("TRACKPAD", screen);
      {
        int y = GUI_HEADER + 40;
        if (ble.isPairingMode()) drawTextCentered(240, y, "PAIRING...", 3);
        else if (ble.isConnected()) drawTextCentered(240, y, "CONNECTED", 3);
        else drawTextCentered(240, y, "WAITING", 3);
        y += 50;
        drawTextCentered(240, y, "1F MOVE+TAP", 2); y += 22;
        drawTextCentered(240, y, "2F SCROLL+PINCH", 2); y += 22;
        drawTextCentered(240, y, "3F MIDDLE+SWIPE", 2); y += 22;
        drawTextCentered(240, y, "EDGE: SCROLL", 2);
      }
      hint = "AI:HOME  UP/DN:PAGE";
      break;
    case SCR_NUMPAD:
      drawHeader("NUMPAD", screen);
      drawKeys(NUMPAD, 3, 4, 5);
      break;
    case SCR_KEYBOARD:
      drawHeader(shifted ? "KEYBOARD ^" : "KEYBOARD", screen);
      drawKeys(KEYBOARD, 10, 5, 2);
      break;
    case SCR_SETTINGS:
      drawHeader("SETTINGS", screen);
      {
        int y = GUI_HEADER + 30;
        drawText(20, y, ble.isConnected() ? "BT: CONNECTED" : (ble.isPairingMode() ? "BT: PAIRING" : "BT: WAITING"), 2); y += 30;
        char b[24]; snprintf(b, sizeof(b), "BONDS: %d", ble.bondCount());
        drawText(20, y, b, 2); y += 30;
        drawText(20, y, "AI SHORT: HOME", 2); y += 26;
        drawText(20, y, "FORGET: SERIAL CMD", 2); y += 26;
        drawText(20, y, "UP/DN: CYCLE SCREEN", 2); y += 26;
        drawText(20, y, "AI IS PWR BUTTON", 2);
      }
      break;
  }
  drawFooter(hint);
  stickyDisplayRefresh();
  dirty = false;
}

// ---------------------------------------------------------------- buttons
static void handleButtons() {
  uint32_t now = millis();
  int pins[3] = {PIN_UP, PIN_DOWN, PIN_AI};
  for (int i = 0; i < 3; ++i) {
    int raw = digitalRead(pins[i]);
    if (raw != lastBtn[i] && (uint32_t)(now - lastBtnChange[i]) > 25) {
      bool pressed = (raw == LOW);
      if (pressed) {
        if (pins[i] == PIN_AI) { aiHeld = true; aiHeldSince = now; aiLongFired = false; }
        else if (pins[i] == PIN_UP)   { screen = (screen + SCR_COUNT - 1) % SCR_COUNT; dirty = true; }
        else if (pins[i] == PIN_DOWN) { screen = (screen + 1) % SCR_COUNT; dirty = true; }
      } else {
        if (pins[i] == PIN_AI && aiHeld) {
          uint32_t dur = now - aiHeldSince;
          if (!aiLongFired && dur < 1500) { screen = SCR_TRACKPAD; dirty = true; } // short press -> home
          aiHeld = false;
        }
      }
      lastBtn[i] = raw;
      lastBtnChange[i] = now;
    }
  }
  // GPIO4 is the board's POWER/OK button: only a short press navigates home.
  // Do NOT map a long press here — the PMIC may power the board off at the
  // hardware level. "Forget host + pair" stays a serial FORGET command.
  if (aiHeld && !aiLongFired && (uint32_t)(now - aiHeldSince) >= 1500) {
    aiLongFired = true;
    aiHeld = false;   // stop treating it as a press; leave power to the PMIC
    Serial.println("[UI] power-button long press ignored (power is hardware-controlled)");
  }
}

// ---------------------------------------------------------------- setup/loop
void setup() {
  pinMode(45, OUTPUT); digitalWrite(45, HIGH);
  pinMode(46, OUTPUT); digitalWrite(46, HIGH);
  Serial.begin(115200); delay(600);
  Serial.println("S8 STICKY GUI");
  Serial.printf("touch=%s ", stickyTouchBegin() ? "OK" : "FAIL");
  Serial.printf("display=%s\n", stickyDisplayBegin() ? "OK" : "FAIL");
  pinMode(PIN_UP, INPUT_PULLUP); pinMode(PIN_DOWN, INPUT_PULLUP); pinMode(PIN_AI, INPUT_PULLUP);
  ble.begin("Sticky Trackpad");
  touchGateUntil = millis() + 400;
  Serial.println("READY");
}

void loop() {
  uint32_t now = millis();

  while (Serial.available()) {
    String c = Serial.readStringUntil('\n'); c.trim();
    if (c == "PING") Serial.println("PONG s8_sticky_gui");
    else if (c == "STATUS") Serial.printf("connected=%u pairing=%u screen=%d shifted=%u bonds=%d reports=%lu\n",
      ble.isConnected(), ble.isPairingMode(), screen, shifted, ble.bondCount(), (unsigned long)ble.reportsSent());
    else if (c == "FORGET") { ble.forgetAllAndPair(); screen = SCR_SETTINGS; dirty = true; }
    else if (c.startsWith("SCREEN")) { int s = c.substring(6).toInt(); if (s >= 0 && s < SCR_COUNT) { screen = s; dirty = true; } }
    else if (c == "SHIFT") { shifted = !shifted; dirty = true; }
  }

  ble.service(now);
  handleButtons();

  if (ble.isConnected() != wasConnected) { wasConnected = ble.isConnected(); dirty = true; touchGateUntil = now + 400; if (!wasConnected) { engine.cancel(); ble.releaseAll(); } }

  StickyTouchPoint pts[5]{};
  int n = (now < touchGateUntil) ? -1 : stickyTouchRead(pts, 5);

  if (screen == SCR_TRACKPAD) {
    if (ble.isConnected() && n >= 0) {
      stickyapp::Contact contacts[3];
      uint8_t count = n > 3 ? 3 : n;
      for (uint8_t i = 0; i < count; ++i) { contacts[i].id = pts[i].id; contacts[i].x = pts[i].x; contacts[i].y = pts[i].y; }
      runAction(engine.update(contacts, count, now));
    } else if (n >= 0) engine.cancel();
  } else if (n >= 0) {
    // simple tap detection on numpad/keyboard/settings
    if (n > 0 && !downing) {
      downX = pts[0].x; downY = pts[0].y; downAt = now; downing = true; moved = false;
    } else if (n > 0 && downing) {
      if (abs((int)pts[0].x - (int)downX) > 18 || abs((int)pts[0].y - (int)downY) > 18) moved = true;
    } else if (n == 0 && downing) {
      if (!moved && (uint32_t)(now - downAt) < 400) handleKeyTap(downX, downY);
      downing = false;
    }
  }

  if (dirty) renderScreen();
  delay(5);
}
