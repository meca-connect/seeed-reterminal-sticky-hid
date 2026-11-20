/*
 * s13_app — Sticky Trackpad app (trackpad + keyboard + numpad, shadcn-style UI).
 *
 * Main screen (no text): dot-grid trackpad, thin scrollbar strip(s), status
 * dot top-centre (filled = connected, ring = not). LONG-PRESS THE DOT (~0.45 s)
 * to open Settings. Buttons: OK/power short press = sleep (sleep screen);
 * UP/DOWN = previous/next mode (Trackpad, Keyboard, Numpad).
 *
 * Gestures: 1F move; tap = click; tap-tap = double click; hold still or
 * tap-and-hold = drag; 2F drag = scroll (both axes); 2F tap = right click;
 * 3F tap = middle click; drag along the scrollbar = vertical scroll.
 * Each kind of click has its own buzzer sound (GPIO48), switchable in Settings.
 *
 * Keyboard / Numpad modes: keys at the bottom, a small trackpad above them in
 * portrait. Keys commit on finger lift (multi-finger rollover), backspace and
 * arrows auto-repeat. Shift: tap = next letter, double tap = caps lock.
 *
 * Display: SSD1677 partial refresh, asynchronous (touch keeps working while
 * the panel updates); full refresh every 10 partials and on leaving Settings.
 *
 * Power: CPU at 80 MHz; loop sleeps on the GT911 INT line between frames;
 * display controller in deep sleep between refreshes; BLE slave latency;
 * TX power 3 dBm; battery level (BQ27220) reported to the host; deep sleep
 * after 10 min without a host connection (wake: touch or any button); OK
 * button sleep powers the touch controller off and wakes on a button only.
 *
 * Serial 115200: PING STATUS STATS FORGET MODE n ROT n SCREEN S|M LAT n
 *                REDRAW FULL BEEP SLEEP SLEEPMIN n
 * FQBN: esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc,FlashMode=dio
 */
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include "hw_epd.h"
#include "hw_misc.h"
#include "hid_ble.h"
#include "app_ui.h"
#include "../../sticky_trackpad/GestureEngine.h"
#include "../../sticky_trackpad/MvpUi.h"

// ------------------------------------------------------------------ state
static Preferences prefs;
static app::Settings S;
static app::Status ST;
static app::KbState KB;
static stickymvp::Rotation rot;
static app::MainLayout ML(480, 800, app::MODE_TRACKPAD, KB);
static stickyapp::GestureEngine engine;
static stickymvp::TapDetector settingsTap;
static stickymvp::ForgetConfirm forgetConfirm;

enum Screen : uint8_t { SCR_MAIN, SCR_SETTINGS };
static Screen screen = SCR_MAIN;
static bool redrawPending = false, redrawFull = false;
static uint32_t lastActivity = 0, sleepAfterMs = 10UL * 60 * 1000;
static uint32_t touchFrames = 0;
static TaskHandle_t loopTask = nullptr;

static void loadPrefs() {
  prefs.begin("sticky13", false);
  S.mode = prefs.getUChar("mode", app::MODE_TRACKPAD) % app::MODE_COUNT;
  S.rotation = prefs.getUChar("rot", 0) & 3;
  S.side = prefs.getUChar("side", app::SIDE_RIGHT) % app::SIDE_COUNT;
  S.speed = prefs.getUChar("speed", app::SPEED_NORMAL) % app::SPEED_COUNT;
  S.sound = prefs.getBool("sound", true);
  hid::wantLatency = prefs.getUChar("lat", 8);
}
static void savePrefs() {
  prefs.putUChar("mode", S.mode); prefs.putUChar("rot", S.rotation); prefs.putUChar("side", S.side);
  prefs.putUChar("speed", S.speed); prefs.putBool("sound", S.sound); prefs.putUChar("lat", hid::wantLatency);
}

static void applySettings() {
  rot.r = S.rotation;
  buzz::enabled = S.sound;
  ML = app::MainLayout(rot.width(), rot.height(), S.mode, KB);
  stickyapp::Tuning t;
  static const float gain[3] = {0.9f, 1.25f, 1.7f}, acc[3] = {0.012f, 0.018f, 0.024f}, mx[3] = {2.0f, 2.6f, 3.4f};
  t.pointerGain = gain[S.speed]; t.pointerAccel = acc[S.speed]; t.pointerAccelMax = mx[S.speed];
  t.edgeWidthPx = app::STRIP_TOUCH;
  t.leftStrip = app::stripLeft(S.side); t.rightStrip = app::stripRight(S.side);
  t.sideStripHorizontal = false; t.sideStripSign = -1.0f;   // scrollbar: drag down = scroll down
  t.stripPxPerTick = 14.0f;
  t.bottomStrip = false;
  engine.setTuning(t);
  if (!ML.pad.empty()) engine.setDimensions(ML.pad.w, ML.pad.h);
}

// ------------------------------------------------------------------ drawing
static void fbSink(void*, int x, int y, bool b) { int p, q; rot.toPortrait(x, y, p, q); stickyDrawPixel(p, q, b); }
static void fbClear(void*) { stickyDisplayClear(); }
static Gfx makeGfx() { Gfx g; g.w = rot.width(); g.h = rot.height(); g.sink = fbSink; g.clearSink = fbClear; return g; }

static void requestRedraw(bool full = false) { redrawPending = true; redrawFull |= full; }
static void serviceRedraw() {
  if (!redrawPending || epdBusy()) return;
  Gfx g = makeGfx();
  uint32_t t0 = millis();
  if (screen == SCR_SETTINGS) app::drawSettings(g, S, ST, forgetConfirm.armed());
  else app::drawMain(g, S, ST, KB);
  uint32_t t1 = millis();
  if (epdStart(redrawFull)) {
    Serial.printf("[UI] %s %s render=%lums send=%lums\n", screen == SCR_SETTINGS ? "settings" : "main",
                  sEpdFull ? "full" : "partial", t1 - t0, millis() - t1);
    redrawPending = redrawFull = false;
  }
}

// ------------------------------------------------------------------ mouse actions
static uint32_t lastClickMs = 0;
static bool dragging = false, dragMoved = false, dragFromDouble = false;

static void runActions(const stickyapp::Actions& a) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < a.count; ++i) {
    const auto& act = a.items[i];
    switch (act.type) {
      case stickyapp::Move:
        hid::mouse(hid::buttons, act.a, act.b, 0, 0);
        if (dragging) dragMoved = true;
        break;
      case stickyapp::Scroll: hid::mouse(hid::buttons, 0, 0, act.b, act.a); break;
      case stickyapp::Click:
        hid::click(act.a);
        if (act.a == 1) { buzz::click(); lastClickMs = now; }
        else if (act.a == 2) buzz::right();
        else buzz::middle();
        break;
      case stickyapp::Press:
        hid::press(act.a);
        dragging = true; dragMoved = false;
        dragFromDouble = now - lastClickMs < 600;
        if (!dragFromDouble) buzz::dragStart();
        break;
      case stickyapp::Release:
        hid::release(act.a);
        if (dragging && dragFromDouble && !dragMoved) buzz::dbl();   // tap-tap = double click
        dragging = false;
        break;
      // 3F swipes -> GNOME: workspaces (Ctrl+Alt+arrows), overview (Super), Esc.
      case stickyapp::SwipeLeft:  hid::tap(0x4F, 0x05); buzz::middle(); break;   // next workspace
      case stickyapp::SwipeRight: hid::tap(0x50, 0x05); buzz::middle(); break;   // previous workspace
      case stickyapp::SwipeUp:    hid::tap(0x00, 0x08); buzz::open(); break;     // Super: Activities
      case stickyapp::SwipeDown:  hid::tap(0x29); buzz::middle(); break;         // Esc: close overview
      default: break;   // pinch: not used
    }
  }
}
static void cancelGestures() { runActions(engine.cancel()); dragging = false; }

// ------------------------------------------------------------------ keys
struct Held { uint8_t id; int8_t key; uint32_t t0, nextRep; bool repeated; };
static Held held[5];
static uint8_t heldN = 0;
static uint32_t lastShiftTap = 0;

static bool repeatable(const app::Key& k) {
  return k.act == app::ACT_USAGE && (k.icon == app::IC_BKSP || (k.icon >= app::IC_LEFT && k.icon <= app::IC_DOWN));
}
static void commitKey(int idx) {
  if (idx < 0 || !ML.kl) return;
  const app::Key& k = ML.kl->keys[idx];
  uint32_t now = millis();
  switch (k.act) {
    case app::ACT_CHAR: {
      char c = k.ch;
      bool letter = c >= 'a' && c <= 'z';
      if (letter && KB.shift != app::SHIFT_OFF) c = char(c - 32);
      uint8_t usage; bool sh;
      if (app::asciiToHid(c, usage, sh)) hid::tap(usage, sh ? 0x02 : 0);
      if (hid::connected) buzz::key();
      if (letter && KB.shift == app::SHIFT_ONCE) { KB.shift = app::SHIFT_OFF; requestRedraw(); }
      break;
    }
    case app::ACT_USAGE: hid::tap(k.usage); if (hid::connected) buzz::key(); break;
    case app::ACT_SHIFT:
      if (KB.shift == app::SHIFT_OFF) KB.shift = app::SHIFT_ONCE;
      else if (KB.shift == app::SHIFT_ONCE && now - lastShiftTap < 400) KB.shift = app::SHIFT_CAPS;
      else KB.shift = app::SHIFT_OFF;
      lastShiftTap = now; buzz::key(); requestRedraw();
      break;
    case app::ACT_LAYER:
      KB.layer = KB.layer == app::LAYER_LETTERS ? app::LAYER_SYMBOLS : app::LAYER_LETTERS;
      KB.shift = app::SHIFT_OFF;
      ML = app::MainLayout(rot.width(), rot.height(), S.mode, KB);
      buzz::key(); requestRedraw();
      break;
  }
}
static void keysFrame(const stickyapp::Contact* c, uint8_t n) {
  uint32_t now = millis();
  for (uint8_t h = 0; h < heldN;) {             // lifted fingers commit their key
    bool still = false;
    for (uint8_t i = 0; i < n; ++i) if (c[i].id == held[h].id) still = true;
    if (!still) {
      if (!held[h].repeated) commitKey(held[h].key);
      held[h] = held[--heldN];
    } else ++h;
  }
  for (uint8_t i = 0; i < n && heldN < 5; ++i) { // new fingers pick a key
    bool known = false;
    for (uint8_t h = 0; h < heldN; ++h) if (held[h].id == c[i].id) known = true;
    if (!known) held[heldN++] = {c[i].id, int8_t(ML.keyAt(int(c[i].x), int(c[i].y))), now, now + 420, false};
  }
}
static void keysRepeat() {
  uint32_t now = millis();
  for (uint8_t h = 0; h < heldN; ++h) {
    if (held[h].key < 0 || !ML.kl || !repeatable(ML.kl->keys[held[h].key])) continue;
    if (int32_t(now - held[h].nextRep) >= 0) {
      hid::tap(ML.kl->keys[held[h].key].usage);
      held[h].repeated = true;
      held[h].nextRep = now + 65;
    }
  }
}

// ------------------------------------------------------------------ settings
static void openSettings() {
  cancelGestures(); heldN = 0;
  screen = SCR_SETTINGS; forgetConfirm.cancel(); settingsTap.reset();
  buzz::open(); requestRedraw();
}
static void closeSettings() {
  savePrefs(); applySettings();
  screen = SCR_MAIN; forgetConfirm.cancel();
  requestRedraw(true);                          // clean full refresh on the main surface
}
static void settingsEvent(const app::Event& e) {
  switch (e.type) {
    case app::EV_MODE: S.mode = uint8_t(e.index); KB = app::KbState(); break;
    case app::EV_SIDE: S.side = uint8_t(e.index); break;
    case app::EV_SPEED: S.speed = uint8_t(e.index); break;
    case app::EV_ROT: S.rotation = uint8_t(e.index); rot.r = S.rotation; break;
    case app::EV_SOUND: S.sound = !S.sound; buzz::enabled = S.sound; break;
    case app::EV_FORGET:
      if (forgetConfirm.press(millis())) { hid::forget(); buzz::warn(); }
      break;
    case app::EV_DONE: buzz::key(); closeSettings(); return;
    default: return;
  }
  if (e.type != app::EV_FORGET) forgetConfirm.cancel();
  buzz::key();
  requestRedraw();
}

// ------------------------------------------------------------------ touch sessions
enum Owner : uint8_t { OWN_NONE, OWN_HANDLE, OWN_PAD, OWN_KEYS, OWN_SETTINGS };
static Owner owner = OWN_NONE;
static uint32_t handleT0 = 0;
static int handleX0 = 0, handleY0 = 0;
static bool handleDead = false;

static void handleService() {
  if (owner != OWN_HANDLE || handleDead) return;
  if (millis() - handleT0 >= 450) { handleDead = true; openSettings(); }
}

static void touchFrame(const StickyTouchPoint* pts, int n) {
  stickyapp::Contact c[5];
  uint8_t cnt = n > 5 ? 5 : uint8_t(n);
  for (uint8_t i = 0; i < cnt; ++i) {
    int sx, sy;
    rot.toScreen(pts[i].x > 479 ? 479 : pts[i].x, pts[i].y > 799 ? 799 : pts[i].y, sx, sy);
    c[i] = {pts[i].id, float(sx), float(sy)};
  }
  if (owner == OWN_NONE) {
    if (!cnt) return;
    int x = int(c[0].x), y = int(c[0].y);
    if (screen == SCR_SETTINGS) owner = OWN_SETTINGS;
    else if (app::inHandle(rot.width(), x, y)) {
      owner = OWN_HANDLE; handleT0 = millis(); handleX0 = x; handleY0 = y; handleDead = false;
    } else if (!ML.pad.empty() && ML.pad.contains(x, y)) owner = OWN_PAD;
    else if (!ML.keys.empty()) owner = OWN_KEYS;
    else owner = OWN_PAD;
  }
  switch (owner) {
    case OWN_HANDLE:
      // Moving off the dot (or a second finger) before the long-press fires
      // hands the touch to the trackpad, so the dot is never a dead spot.
      if (cnt && !handleDead && (cnt > 1 || abs(int(c[0].x) - handleX0) > 24 || abs(int(c[0].y) - handleY0) > 24)
          && !ML.pad.empty()) { handleDead = true; owner = OWN_PAD; }
      else break;
      [[fallthrough]];
    case OWN_PAD:
      if (hid::connected && !ML.pad.empty()) {
        for (uint8_t i = 0; i < cnt; ++i) {
          float x = c[i].x - ML.pad.x, y = c[i].y - ML.pad.y;
          c[i].x = x < 0 ? 0 : (x > ML.pad.w - 1 ? ML.pad.w - 1 : x);
          c[i].y = y < 0 ? 0 : (y > ML.pad.h - 1 ? ML.pad.h - 1 : y);
        }
        runActions(engine.update(c, cnt, millis()));
      }
      break;
    case OWN_KEYS: keysFrame(c, cnt); break;
    case OWN_SETTINGS: {
      int tx, ty;
      if (settingsTap.update(cnt, cnt ? int(c[0].x) : 0, cnt ? int(c[0].y) : 0, millis(), tx, ty))
        settingsEvent(app::SettingsLayout(rot.width(), rot.height()).hit(tx, ty));
      break;
    }
    default: break;
  }
  if (!cnt) owner = OWN_NONE;
}

static void IRAM_ATTR touchIsr() {
  BaseType_t woke = pdFALSE;
  if (loopTask) vTaskNotifyGiveFromISR(loopTask, &woke);
  if (woke) portYIELD_FROM_ISR();
}

// ------------------------------------------------------------------ power
// manual = put to sleep with the OK/power button: wakes on a button only (touch
// powered off, so a bag or pocket can't wake it). Idle sleep also wakes on touch.
static void enterDeepSleep(bool manual) {
  Serial.printf("[PWR] deep sleep (%s; wake: %s)\n", manual ? "button" : "idle", manual ? "button" : "touch or button");
  cancelGestures(); heldN = 0;
  buzz::sleep();
  while (epdBusy()) { epdPoll(); delay(5); }
  screen = SCR_MAIN;
  Gfx g = makeGfx();
  app::drawSleep(g, ST, manual);
  epdStart(true);                                // full refresh: crisp, ghost-free sleep screen
  uint32_t t0 = millis();
  while ((epdBusy() || buzz::qi < buzz::qn) && millis() - t0 < 5000) { epdPoll(); buzz::service(); delay(5); }
  buzz::out(0);
  Serial.flush();
  detachInterrupt(21);
  if (manual) { digitalWrite(42, LOW); }         // touch controller off
  for (gpio_num_t p : {GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_42, GPIO_NUM_39}) gpio_hold_en(p);   // 39: keep charging
  gpio_deep_sleep_hold_en();
  if (!manual) {
    int idle = digitalRead(21);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, idle ? 0 : 1);
  }
  for (gpio_num_t p : {GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6}) { rtc_gpio_pullup_en(p); rtc_gpio_pulldown_dis(p); }
  esp_sleep_enable_ext1_wakeup((1ULL << 4) | (1ULL << 5) | (1ULL << 6), ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ buttons
// Active low: OK/power (GPIO4) short press = sleep; UP/DOWN cycle
// Trackpad -> Keyboard -> Numpad. Actions fire on release; a button already
// held at boot (the one that woke the device) is ignored until released.
// Long-pressing OK is left alone: the board's power circuit uses it.
struct Btn { uint8_t pin; bool down, armed; uint32_t t; };
static Btn btns[3] = {{4, false, false, 0}, {5, false, false, 0}, {6, false, false, 0}};
static void buttonsBegin() {
  for (Btn& b : btns) {
    rtc_gpio_deinit(gpio_num_t(b.pin));          // back from RTC (deep-sleep wake) to digital
    pinMode(b.pin, INPUT_PULLUP);
    b.down = digitalRead(b.pin) == LOW; b.armed = !b.down; b.t = millis();
  }
}
static void setMode(uint8_t m) {
  cancelGestures(); heldN = 0; owner = OWN_NONE;
  if (screen == SCR_SETTINGS) { screen = SCR_MAIN; forgetConfirm.cancel(); }
  S.mode = m % app::MODE_COUNT; KB = app::KbState();
  savePrefs(); applySettings();
  buzz::open(); requestRedraw();
  Serial.printf("[BTN] mode=%u\n", S.mode);
}
static void buttonsService() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 3; ++i) {
    Btn& b = btns[i];
    bool d = digitalRead(b.pin) == LOW;
    if (d == b.down || now - b.t < 30) continue;  // 30 ms debounce
    uint32_t held = now - b.t;
    b.down = d; b.t = now;
    if (d) continue;                              // act on release
    if (!b.armed) { b.armed = true; continue; }
    lastActivity = now;
    if (i == 0) { if (held < 1500) enterDeepSleep(true); }
    else setMode(i == 1 ? S.mode + app::MODE_COUNT - 1 : S.mode + 1);
  }
}

// ------------------------------------------------------------------ serial
static void handleSerial() {
  while (Serial.available()) {
    String c = Serial.readStringUntil('\n'); c.trim();
    if (c == "PING") Serial.println("PONG s13_app");
    else if (c == "STATUS")
      Serial.printf("connected=%u bonds=%d screen=%u mode=%u rot=%u side=%u speed=%u sound=%u batt=%d%% %dmV %dmA chg=%u\n",
                    hid::connected, hid::bonds(), screen, S.mode, S.rotation * 90, S.side, S.speed, S.sound,
                    batt::pct, batt::mv, batt::ma, batt::chargerActive());
    else if (c == "STATS")
      Serial.printf("frames=%lu notifies=%lu fail=%lu interval=%.2fms latency=%u epd_last=%lums cpu=%luMHz heap=%u\n",
                    touchFrames, hid::notifies, hid::notifyFail, hid::intervalUnits * 1.25f, hid::latency,
                    sEpdLastMs, getCpuFrequencyMhz(), ESP.getFreeHeap());
    else if (c == "FORGET") { hid::forget(); requestRedraw(); }
    else if (c.startsWith("MODE")) { S.mode = c.substring(4).toInt() % app::MODE_COUNT; KB = app::KbState(); savePrefs(); applySettings(); requestRedraw(true); }
    else if (c.startsWith("ROT")) { S.rotation = (c.substring(3).toInt() / 90) & 3; savePrefs(); applySettings(); requestRedraw(true); }
    else if (c == "SCREEN S") openSettings();
    else if (c == "SCREEN M") closeSettings();
    else if (c.startsWith("LAT")) { hid::wantLatency = c.substring(3).toInt(); savePrefs(); Serial.printf("latency request=%u (next connect)\n", hid::wantLatency); }
    else if (c == "REDRAW") requestRedraw();
    else if (c == "FULL") requestRedraw(true);
    else if (c == "BEEP") { buzz::play({{330, 40}, {0, 30}, {440, 40}}, true); }
    else if (c == "SLEEP") enterDeepSleep(true);
    else if (c.startsWith("SLEEPMIN")) { sleepAfterMs = uint32_t(c.substring(8).toInt()) * 60000UL; Serial.printf("sleep after %lu ms\n", sleepAfterMs); }
    else if (c.length()) Serial.printf("? %s\n", c.c_str());
  }
}

// ------------------------------------------------------------------ setup / loop
void setup() {
  pinMode(45, OUTPUT); digitalWrite(45, HIGH);   // board power hold
  pinMode(46, OUTPUT); digitalWrite(46, HIGH);
  for (gpio_num_t p : {GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_42, GPIO_NUM_39}) gpio_hold_dis(p);
  setCpuFrequencyMhz(80);
  Serial.begin(115200); delay(300);
  Serial.printf("S13 APP — wake cause %d, cpu %lu MHz\n", int(esp_sleep_get_wakeup_cause()), getCpuFrequencyMhz());
  loadPrefs();
  rtc_gpio_deinit(GPIO_NUM_21);                  // touch INT back to digital after an ext0 wake
  buttonsBegin();
  buzz::begin();
  batt::begin();
  ST.battery = batt::ok ? batt::pct : -1; ST.charging = batt::charging();
  Serial.printf("Battery: %s %d%% %dmV %dmA\n", batt::ok ? "OK" : "n/a", batt::pct, batt::mv, batt::ma);
  Serial.printf("Touch(400kHz): %s\n", stickyTouchBegin(400000) ? "OK" : "FAILED");
  loopTask = xTaskGetCurrentTaskHandle();
  attachInterrupt(21, touchIsr, CHANGE);
  Serial.printf("Display: %s\n", epdBegin() ? "OK" : "FAILED");
  applySettings();
  hid::begin("Sticky Trackpad");
  hid::battery(ST.battery);
  ST.pairing = hid::pairing();
  requestRedraw(true);
  serviceRedraw();
  lastActivity = millis();
  Serial.println("READY");
}

void loop() {
  static bool wasConnected = false, wasPairing = false;
  static int drawnLowBatt = -1;
  static uint32_t bootGate = millis() + 400;

  handleSerial();
  hid::service();
  buzz::service();
  if (epdPoll()) Serial.printf("[EPD] %s done in %lums\n", sEpdFull ? "full" : "partial", sEpdLastMs);

  if (batt::service()) {
    ST.battery = batt::ok ? batt::pct : -1; ST.charging = batt::charging();
    hid::battery(ST.battery);
    int low = ST.battery >= 0 && ST.battery <= 15 && !ST.charging;
    if (low != drawnLowBatt || screen == SCR_SETTINGS) { drawnLowBatt = low; requestRedraw(); }
  }
  bool conn = hid::connected, pair = hid::pairing();
  if (conn != wasConnected || pair != wasPairing) {
    if (conn && !wasConnected) buzz::connected();
    if (!conn && wasConnected) { buzz::lost(); cancelGestures(); }
    wasConnected = conn; wasPairing = pair;
    ST.connected = conn; ST.pairing = pair;
    lastActivity = millis();
    requestRedraw();
  }
  if (screen == SCR_SETTINGS && forgetConfirm.expired(millis())) requestRedraw();
  serviceRedraw();
  handleService();
  keysRepeat();
  buttonsService();

  if (!conn && sleepAfterMs && millis() - lastActivity > sleepAfterMs && !redrawPending) enterDeepSleep(false);

  // Sleep until the touch controller signals (or a short timeout for timers).
  bool busyTimers = owner != OWN_NONE || buzz::qi < buzz::qn || heldN;
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(busyTimers ? 3 : 25));

  StickyTouchPoint pts[5]{};
  int n = stickyTouchRead(pts, 5);
  if (n < 0) return;
  if (int32_t(millis() - bootGate) < 0) return;   // boot-time phantom frames
  ++touchFrames;
  lastActivity = millis();
  touchFrame(pts, n);
}
