#pragma once
// Sticky app UI: layouts, drawing and hit-testing for every screen. Portable
// C++ (no Arduino), host-tested and rendered to PNG by tests/test_app_ui.cpp.
// Visual language follows shadcn/ui: Geist type, 2px outlines, rounded
// corners, muted (dithered) tracks, one filled "primary" action per screen.
#include "gfx.h"

namespace app {

enum Mode : uint8_t { MODE_TRACKPAD, MODE_KEYBOARD, MODE_NUMPAD, MODE_COUNT };
enum Side : uint8_t { SIDE_RIGHT, SIDE_LEFT, SIDE_BOTH, SIDE_OFF, SIDE_COUNT };
enum Speed : uint8_t { SPEED_SLOW, SPEED_NORMAL, SPEED_FAST, SPEED_COUNT };

struct Settings {
  uint8_t mode = MODE_TRACKPAD;
  uint8_t rotation = 0;     // 0..3, x90 degrees
  uint8_t side = SIDE_RIGHT;
  uint8_t speed = SPEED_NORMAL;
  bool sound = true;
};
struct Status {
  bool connected = false;
  bool pairing = false;     // advertising with no saved host
  int battery = -1;         // percent, -1 unknown
  bool charging = false;
};
enum Layer : uint8_t { LAYER_LETTERS, LAYER_SYMBOLS };
enum ShiftState : uint8_t { SHIFT_OFF, SHIFT_ONCE, SHIFT_CAPS };
struct KbState { uint8_t layer = LAYER_LETTERS; uint8_t shift = SHIFT_OFF; };

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
};

// ------------------------------------------------------------ main screens
static constexpr int HANDLE_H = 64;        // top band that owns the status dot
static constexpr int HANDLE_W = 140;
static constexpr int DOT_R = 9;
static constexpr int STRIP_TOUCH = 34;     // scrollbar touch width (~3.5 mm)
static constexpr int MARGIN = 10, GAP = 6;

static constexpr int HANDLE_TOUCH_R = 36;  // only this circle around the dot opens Settings
inline int dotX(int W) { return W / 2; }
inline int dotY() { return HANDLE_H / 2 - 4; }
inline bool inHandle(int W, int x, int y) {
  int dx = x - dotX(W), dy = y - dotY();
  return dx * dx + dy * dy <= HANDLE_TOUCH_R * HANDLE_TOUCH_R;
}

// Key model, in layout grid units.
enum KeyAct : uint8_t { ACT_CHAR, ACT_USAGE, ACT_SHIFT, ACT_LAYER };
enum KeyIcon : uint8_t { IC_NONE, IC_BKSP, IC_SHIFT, IC_ENTER, IC_LEFT, IC_UP, IC_RIGHT, IC_DOWN };
struct Key {
  const char* label;  // drawn label (letters drawn per shift state)
  char ch;            // ACT_CHAR: ASCII to type
  uint8_t usage;      // ACT_USAGE: HID usage
  uint8_t act, icon;
  bool special;       // muted background
  float x, y, w, h;
};
struct KeyLayout { const Key* keys; int count; float gw, gh; };

#define K(c) {#c, (#c)[0], 0, ACT_CHAR, IC_NONE, false
static const Key KB_LETTERS[] = {
  {"esc", 0, 0x29, ACT_USAGE, IC_NONE, true, 0, 0, 2, .8f}, {"tab", 0, 0x2B, ACT_USAGE, IC_NONE, true, 2, 0, 2, .8f},
  {"", 0, 0x50, ACT_USAGE, IC_LEFT, true, 4, 0, 1.5f, .8f}, {"", 0, 0x52, ACT_USAGE, IC_UP, true, 5.5f, 0, 1.5f, .8f},
  {"", 0, 0x51, ACT_USAGE, IC_DOWN, true, 7, 0, 1.5f, .8f}, {"", 0, 0x4F, ACT_USAGE, IC_RIGHT, true, 8.5f, 0, 1.5f, .8f},
  K(q), 0, .8f, 1, 1}, K(w), 1, .8f, 1, 1}, K(e), 2, .8f, 1, 1}, K(r), 3, .8f, 1, 1}, K(t), 4, .8f, 1, 1},
  K(y), 5, .8f, 1, 1}, K(u), 6, .8f, 1, 1}, K(i), 7, .8f, 1, 1}, K(o), 8, .8f, 1, 1}, K(p), 9, .8f, 1, 1},
  K(a), .5f, 1.8f, 1, 1}, K(s), 1.5f, 1.8f, 1, 1}, K(d), 2.5f, 1.8f, 1, 1}, K(f), 3.5f, 1.8f, 1, 1},
  K(g), 4.5f, 1.8f, 1, 1}, K(h), 5.5f, 1.8f, 1, 1}, K(j), 6.5f, 1.8f, 1, 1}, K(k), 7.5f, 1.8f, 1, 1},
  K(l), 8.5f, 1.8f, 1, 1},
  {"", 0, 0, ACT_SHIFT, IC_SHIFT, true, 0, 2.8f, 1.5f, 1},
  K(z), 1.5f, 2.8f, 1, 1}, K(x), 2.5f, 2.8f, 1, 1}, K(c), 3.5f, 2.8f, 1, 1}, K(v), 4.5f, 2.8f, 1, 1},
  K(b), 5.5f, 2.8f, 1, 1}, K(n), 6.5f, 2.8f, 1, 1}, K(m), 7.5f, 2.8f, 1, 1},
  {"", 0, 0x2A, ACT_USAGE, IC_BKSP, true, 8.5f, 2.8f, 1.5f, 1},
  {"123", 0, 0, ACT_LAYER, IC_NONE, true, 0, 3.8f, 2, 1}, {",", ',', 0, ACT_CHAR, IC_NONE, false, 2, 3.8f, 1, 1},
  {"space", ' ', 0, ACT_CHAR, IC_NONE, false, 3, 3.8f, 4, 1}, {".", '.', 0, ACT_CHAR, IC_NONE, false, 7, 3.8f, 1, 1},
  {"", 0, 0x28, ACT_USAGE, IC_ENTER, true, 8, 3.8f, 2, 1},
};
static const Key KB_SYMBOLS[] = {
  {"esc", 0, 0x29, ACT_USAGE, IC_NONE, true, 0, 0, 2, .8f}, {"tab", 0, 0x2B, ACT_USAGE, IC_NONE, true, 2, 0, 2, .8f},
  {"", 0, 0x50, ACT_USAGE, IC_LEFT, true, 4, 0, 1.5f, .8f}, {"", 0, 0x52, ACT_USAGE, IC_UP, true, 5.5f, 0, 1.5f, .8f},
  {"", 0, 0x51, ACT_USAGE, IC_DOWN, true, 7, 0, 1.5f, .8f}, {"", 0, 0x4F, ACT_USAGE, IC_RIGHT, true, 8.5f, 0, 1.5f, .8f},
  K(1), 0, .8f, 1, 1}, K(2), 1, .8f, 1, 1}, K(3), 2, .8f, 1, 1}, K(4), 3, .8f, 1, 1}, K(5), 4, .8f, 1, 1},
  K(6), 5, .8f, 1, 1}, K(7), 6, .8f, 1, 1}, K(8), 7, .8f, 1, 1}, K(9), 8, .8f, 1, 1}, K(0), 9, .8f, 1, 1},
  {"-", '-', 0, ACT_CHAR, IC_NONE, false, 0, 1.8f, 1, 1}, {"/", '/', 0, ACT_CHAR, IC_NONE, false, 1, 1.8f, 1, 1},
  {":", ':', 0, ACT_CHAR, IC_NONE, false, 2, 1.8f, 1, 1}, {";", ';', 0, ACT_CHAR, IC_NONE, false, 3, 1.8f, 1, 1},
  {"(", '(', 0, ACT_CHAR, IC_NONE, false, 4, 1.8f, 1, 1}, {")", ')', 0, ACT_CHAR, IC_NONE, false, 5, 1.8f, 1, 1},
  {"$", '$', 0, ACT_CHAR, IC_NONE, false, 6, 1.8f, 1, 1}, {"&", '&', 0, ACT_CHAR, IC_NONE, false, 7, 1.8f, 1, 1},
  {"@", '@', 0, ACT_CHAR, IC_NONE, false, 8, 1.8f, 1, 1}, {"\"", '"', 0, ACT_CHAR, IC_NONE, false, 9, 1.8f, 1, 1},
  {"=", '=', 0, ACT_CHAR, IC_NONE, true, 0, 2.8f, 1.5f, 1},
  {"?", '?', 0, ACT_CHAR, IC_NONE, false, 1.5f, 2.8f, 1, 1}, {"!", '!', 0, ACT_CHAR, IC_NONE, false, 2.5f, 2.8f, 1, 1},
  {"'", '\'', 0, ACT_CHAR, IC_NONE, false, 3.5f, 2.8f, 1, 1}, {"*", '*', 0, ACT_CHAR, IC_NONE, false, 4.5f, 2.8f, 1, 1},
  {"#", '#', 0, ACT_CHAR, IC_NONE, false, 5.5f, 2.8f, 1, 1}, {"+", '+', 0, ACT_CHAR, IC_NONE, false, 6.5f, 2.8f, 1, 1},
  {"_", '_', 0, ACT_CHAR, IC_NONE, false, 7.5f, 2.8f, 1, 1},
  {"", 0, 0x2A, ACT_USAGE, IC_BKSP, true, 8.5f, 2.8f, 1.5f, 1},
  {"abc", 0, 0, ACT_LAYER, IC_NONE, true, 0, 3.8f, 2, 1}, {"<", '<', 0, ACT_CHAR, IC_NONE, false, 2, 3.8f, 1, 1},
  {"space", ' ', 0, ACT_CHAR, IC_NONE, false, 3, 3.8f, 4, 1}, {">", '>', 0, ACT_CHAR, IC_NONE, false, 7, 3.8f, 1, 1},
  {"", 0, 0x28, ACT_USAGE, IC_ENTER, true, 8, 3.8f, 2, 1},
};
static const Key NUMPAD[] = {
  {"tab", 0, 0x2B, ACT_USAGE, IC_NONE, true, 0, 0, 1, 1}, {"/", '/', 0, ACT_CHAR, IC_NONE, true, 1, 0, 1, 1},
  {"*", '*', 0, ACT_CHAR, IC_NONE, true, 2, 0, 1, 1}, {"", 0, 0x2A, ACT_USAGE, IC_BKSP, true, 3, 0, 1, 1},
  K(7), 0, 1, 1, 1}, K(8), 1, 1, 1, 1}, K(9), 2, 1, 1, 1}, {"-", '-', 0, ACT_CHAR, IC_NONE, true, 3, 1, 1, 1},
  K(4), 0, 2, 1, 1}, K(5), 1, 2, 1, 1}, K(6), 2, 2, 1, 1}, {"+", '+', 0, ACT_CHAR, IC_NONE, true, 3, 2, 1, 1},
  K(1), 0, 3, 1, 1}, K(2), 1, 3, 1, 1}, K(3), 2, 3, 1, 1}, {"", 0, 0x28, ACT_USAGE, IC_ENTER, true, 3, 3, 1, 2},
  K(0), 0, 4, 2, 1}, {".", '.', 0, ACT_CHAR, IC_NONE, false, 2, 4, 1, 1},
};
#undef K
static constexpr KeyLayout LAYOUT_LETTERS{KB_LETTERS, int(sizeof(KB_LETTERS) / sizeof(Key)), 10, 4.8f};
static constexpr KeyLayout LAYOUT_SYMBOLS{KB_SYMBOLS, int(sizeof(KB_SYMBOLS) / sizeof(Key)), 10, 4.8f};
static constexpr KeyLayout LAYOUT_NUMPAD{NUMPAD, int(sizeof(NUMPAD) / sizeof(Key)), 4, 5};

inline const KeyLayout* layoutFor(uint8_t mode, const KbState& kb) {
  if (mode == MODE_KEYBOARD) return kb.layer == LAYER_SYMBOLS ? &LAYOUT_SYMBOLS : &LAYOUT_LETTERS;
  if (mode == MODE_NUMPAD) return &LAYOUT_NUMPAD;
  return nullptr;
}

// Region geometry for a main screen: keys at the bottom, a trackpad above
// them when there is room (>=180 px), otherwise keys fill the screen.
struct MainLayout {
  Rect pad;    // trackpad surface (engine space), empty if none
  Rect keys;   // key area, empty in trackpad mode
  float unitW = 0, unitH = 0;
  const KeyLayout* kl = nullptr;
  MainLayout(int W, int H, uint8_t mode, const KbState& kb) {
    kl = layoutFor(mode, kb);
    if (!kl) { pad = {0, 0, W, H}; return; }   // the whole surface is trackpad
    int kw = W - 2 * MARGIN;
    float uh = mode == MODE_NUMPAD ? 92.f : 78.f;   // comfortable key heights
    int kh = int(uh * kl->gh);
    int maxKh = H - HANDLE_H - MARGIN;
    if (H - HANDLE_H - kh - MARGIN < 180) { kh = maxKh; }
    keys = {MARGIN, H - MARGIN - kh, kw, kh};
    unitW = float(kw) / kl->gw; unitH = float(kh) / kl->gh;
    int padH = keys.y - GAP;
    if (padH >= 180 + HANDLE_H) pad = {0, 0, W, padH};
  }
  Rect keyRect(const Key& k) const {
    int x0 = keys.x + int(k.x * unitW + .5f), x1 = keys.x + int((k.x + k.w) * unitW + .5f);
    int y0 = keys.y + int(k.y * unitH + .5f), y1 = keys.y + int((k.y + k.h) * unitH + .5f);
    return {x0 + GAP / 2, y0 + GAP / 2, x1 - x0 - GAP, y1 - y0 - GAP};
  }
  // Nearest key wins inside the key area, so gaps never swallow a tap.
  int keyAt(int x, int y) const {
    if (!kl || !keys.contains(x, y)) return -1;
    int best = -1; long bestD = 0x7fffffff;
    for (int i = 0; i < kl->count; ++i) {
      Rect r = keyRect(kl->keys[i]);
      int dx = x < r.x ? r.x - x : (x >= r.x + r.w ? x - (r.x + r.w - 1) : 0);
      int dy = y < r.y ? r.y - y : (y >= r.y + r.h ? y - (r.y + r.h - 1) : 0);
      long d = long(dx) * dx + long(dy) * dy;
      if (d < bestD) { bestD = d; best = i; }
    }
    return best;
  }
};

// Scrollbar strips, in pad-local coordinates.
inline bool stripLeft(uint8_t side) { return side == SIDE_LEFT || side == SIDE_BOTH; }
inline bool stripRight(uint8_t side) { return side == SIDE_RIGHT || side == SIDE_BOTH; }

// ------------------------------------------------------------ US keyboard map
// ASCII -> HID usage (+shift). Returns false for unmapped characters.
inline bool asciiToHid(char c, uint8_t& usage, bool& shift) {
  shift = false;
  if (c >= 'a' && c <= 'z') { usage = uint8_t(0x04 + c - 'a'); return true; }
  if (c >= 'A' && c <= 'Z') { usage = uint8_t(0x04 + c - 'A'); shift = true; return true; }
  if (c >= '1' && c <= '9') { usage = uint8_t(0x1E + c - '1'); return true; }
  if (c == '0') { usage = 0x27; return true; }
  static const char* plain = " -=[]\\;'`,./";
  static const uint8_t plainU[] = {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
  static const char* shifted = "!@#$%^&*()_+{}|:\"~<>?";
  static const uint8_t shiftedU[] = {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                                     0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
  for (int i = 0; plain[i]; ++i) if (plain[i] == c) { usage = plainU[i]; return true; }
  for (int i = 0; shifted[i]; ++i) if (shifted[i] == c) { usage = shiftedU[i]; shift = true; return true; }
  return false;
}

// ------------------------------------------------------------ settings
enum EvType : uint8_t { EV_NONE, EV_MODE, EV_SIDE, EV_SPEED, EV_ROT, EV_SOUND, EV_FORGET, EV_DONE };
struct Event { EvType type = EV_NONE; int index = 0; };

enum SegId : uint8_t { SEG_MODE, SEG_SIDE, SEG_SPEED, SEG_ROT, SEG_COUNT };
static const char* const SEG_TITLE[SEG_COUNT] = {"Mode", "Scrollbar", "Pointer speed", "Rotation"};
static const char* const SEG_MODE_L[] = {"Trackpad", "Keyboard", "Numpad"};
static const char* const SEG_SIDE_L[] = {"Right", "Left", "Both", "Off"};
static const char* const SEG_SPEED_L[] = {"Slow", "Normal", "Fast"};
static const char* const SEG_ROT_L[] = {"0", "90", "180", "270"};
static const char* const* const SEG_LABELS[SEG_COUNT] = {SEG_MODE_L, SEG_SIDE_L, SEG_SPEED_L, SEG_ROT_L};
static const uint8_t SEG_N[SEG_COUNT] = {3, 4, 3, 4};

struct SettingsLayout {
  static constexpr int PADX = 24, TOP = 24, HEADER = 128, LABEL = 34, SEG_H = 56, SECTION_GAP = 18,
                       ROW_H = 56, BTN_H = 60;
  Rect header, seg[SEG_COUNT], sound, forget, done;
  SettingsLayout(int W, int H) {
    bool twoCol = W > H;
    int colW = twoCol ? (W - 3 * PADX) / 2 : W - 2 * PADX;
    int x0 = PADX, x1 = twoCol ? 2 * PADX + colW : PADX;
    header = {x0, TOP, colW, HEADER - TOP};
    int y = HEADER;
    auto section = [&](int x, int& yy) { Rect r{x, yy + LABEL, colW, SEG_H}; yy += LABEL + SEG_H + SECTION_GAP; return r; };
    seg[SEG_MODE] = section(x0, y);
    seg[SEG_SIDE] = section(x0, y);
    if (twoCol) y = HEADER - 60;       // right column sits just below the title line
    seg[SEG_SPEED] = section(x1, y);
    seg[SEG_ROT] = section(x1, y);
    sound = {x1, y, colW, ROW_H};
    int by = H - PADX - BTN_H, bw = (colW - 12) / 2;
    forget = {x1, by, bw, BTN_H};
    done = {x1 + bw + 12, by, colW - bw - 12, BTN_H};
  }
  Event hit(int x, int y) const {
    for (int s = 0; s < SEG_COUNT; ++s)
      if (seg[s].contains(x, y)) {
        int i = (x - seg[s].x) * SEG_N[s] / seg[s].w;
        return {EvType(EV_MODE + s), i < 0 ? 0 : (i >= SEG_N[s] ? SEG_N[s] - 1 : i)};
      }
    if (sound.contains(x, y)) return {EV_SOUND, 0};
    if (forget.contains(x, y)) return {EV_FORGET, 0};
    if (done.contains(x, y)) return {EV_DONE, 0};
    return {};
  }
};

// ------------------------------------------------------------ drawing
inline void drawStatusDot(const Gfx& g, const Status& st) {
  int cx = dotX(g.w), cy = dotY();
  if (st.connected) g.disc(cx, cy, DOT_R);
  else g.ring(cx, cy, DOT_R, 3);
  if (st.battery >= 0 && st.battery <= 15 && !st.charging)   // quiet low-battery cue
    icon::battery(g, cx + 22, cy - 8, 30, 16, st.battery, false);
}

inline void drawPad(const Gfx& g, const Rect& pad, uint8_t side) {
  const int L = stripLeft(side) ? STRIP_TOUCH : 0, R = stripRight(side) ? STRIP_TOUCH : 0;
  for (int y = pad.y + 28; y < pad.y + pad.h - 12; y += 40)
    for (int x = pad.x + L + 28; x < pad.x + pad.w - R - 12; x += 40)
      if (!inHandle(g.w, x, y)) g.fill(x - 1, y - 1, 3, 3, INK);   // keep the dot clear
  auto track = [&](int cx) {   // thin rounded scrollbar track with a grip
    int top = pad.y + 24, len = pad.h - 48;
    if (len < 40) return;
    g.fillRound(cx - 3, top, 6, len, 3, GRAY50);
    g.fillRound(cx - 3, top + len / 2 - 36, 6, 72, 3, INK);
  };
  if (L) track(pad.x + L / 2);
  if (R) track(pad.x + pad.w - R / 2 - 1);
}

inline void drawKey(const Gfx& g, const Rect& r, const Key& k, const KbState& kb) {
  bool primary = k.icon == IC_ENTER;
  bool shiftOn = k.act == ACT_SHIFT && kb.shift != SHIFT_OFF;
  bool filled = primary || shiftOn;
  const int rad = 10;
  if (filled) g.fillRound(r.x, r.y, r.w, r.h, rad, INK);
  else {
    if (k.special) g.fillRound(r.x, r.y, r.w, r.h, rad, GRAY12);
    g.strokeRound(r.x, r.y, r.w, r.h, rad, 2);
  }
  bool ink = !filled;
  int s = (r.h < r.w ? r.h : r.w) * 6 / 10;
  int ix = r.x + (r.w - s) / 2, iy = r.y + (r.h - s) / 2;
  switch (k.icon) {
    case IC_BKSP: icon::backspace(g, ix, iy, s, ink); return;
    case IC_SHIFT:
      icon::shift(g, ix, iy, s, kb.shift != SHIFT_OFF, ink ? true : false);
      if (kb.shift == SHIFT_CAPS) g.fill(ix + s / 3, iy + s, s / 3, 3, PAPER);
      return;
    case IC_ENTER: icon::enter(g, ix, iy, s, ink); return;
    case IC_LEFT: icon::arrow(g, ix, iy, s, 0, ink); return;
    case IC_UP: icon::arrow(g, ix, iy, s, 1, ink); return;
    case IC_RIGHT: icon::arrow(g, ix, iy, s, 2, ink); return;
    case IC_DOWN: icon::arrow(g, ix, iy, s, 3, ink); return;
    default: break;
  }
  char buf[8];
  const char* label = k.label;
  bool upper = kb.shift != SHIFT_OFF && k.act == ACT_CHAR && k.ch >= 'a' && k.ch <= 'z';
  if (upper) { buf[0] = char(k.ch - 32); buf[1] = 0; label = buf; }
  const StickyFont& f = (label[0] && !label[1]) ? geist_key : geist_med;
  g.textIn(f, r.x, r.y, r.w, r.h, label, ink);
}

inline void drawMain(const Gfx& g, const Settings& s, const Status& st, const KbState& kb) {
  g.clear();
  MainLayout L(g.w, g.h, s.mode, kb);
  if (!L.pad.empty()) drawPad(g, L.pad, s.side);
  if (L.kl)
    for (int i = 0; i < L.kl->count; ++i) drawKey(g, L.keyRect(L.kl->keys[i]), L.kl->keys[i], kb);
  drawStatusDot(g, st);
}

// Largest of medium/body/small that fits the width.
inline const StickyFont& fitFont(const char* const* labels, int n, int maxW) {
  const StickyFont* fonts[3] = {&geist_med, &geist_body, &geist_sm};
  for (const StickyFont* f : fonts) {
    bool ok = true;
    for (int i = 0; i < n; ++i) if (Gfx::textWidth(*f, labels[i]) > maxW) ok = false;
    if (ok) return *f;
  }
  return geist_sm;
}

inline void drawSegmented(const Gfx& g, const Rect& r, const char* title, const char* const* labels, int n, int sel) {
  g.text(geist_sm, r.x + 2, r.y - 10, title);
  g.fillRound(r.x, r.y, r.w, r.h, 12, GRAY12);
  g.strokeRound(r.x, r.y, r.w, r.h, 12, 1, GRAY50);
  int segW = r.w / n;
  const StickyFont& f = fitFont(labels, n, segW - 24);
  for (int i = 0; i < n; ++i) {
    int sx = r.x + i * segW, sw = (i == n - 1) ? r.w - i * segW : segW;
    if (i == sel) {
      g.fillRound(sx + 4, r.y + 4, sw - 8, r.h - 8, 9, PAPER);
      g.strokeRound(sx + 4, r.y + 4, sw - 8, r.h - 8, 9, 2);
    }
    g.textIn(f, sx, r.y, sw, r.h, labels[i]);
  }
}

inline void drawSwitch(const Gfx& g, int x, int y, bool on) {
  const int w = 76, h = 42;
  if (on) {
    g.fillRound(x, y, w, h, h / 2, INK);
    g.disc(x + w - h / 2, y + h / 2, h / 2 - 6, PAPER);
  } else {
    g.fillRound(x, y, w, h, h / 2, GRAY25);
    g.strokeRound(x, y, w, h, h / 2, 2);
    g.disc(x + h / 2, y + h / 2, h / 2 - 7, PAPER);
    g.ring(x + h / 2, y + h / 2, h / 2 - 7, 2);
  }
}

inline void drawBadge(const Gfx& g, int x, int y, const char* text, bool solid) {
  int tw = Gfx::textWidth(geist_sm, text), w = tw + 46, h = 36;
  if (solid) g.fillRound(x, y, w, h, h / 2, INK); else g.strokeRound(x, y, w, h, h / 2, 2);
  if (solid) g.disc(x + 18, y + h / 2, 5, PAPER); else g.ring(x + 18, y + h / 2, 6, 2);
  g.text(geist_sm, x + 32, Gfx::capBaseline(geist_sm, y, h), text, !solid);
}

inline void drawButton(const Gfx& g, const Rect& r, const char* label, bool primary) {
  if (primary) g.fillRound(r.x, r.y, r.w, r.h, 12, INK); else g.strokeRound(r.x, r.y, r.w, r.h, 12, 2);
  const char* one[1] = {label};
  g.textIn(fitFont(one, 1, r.w - 28), r.x, r.y, r.w, r.h, label, !primary);
}

inline void drawSettings(const Gfx& g, const Settings& s, const Status& st, bool forgetArmed) {
  g.clear();
  SettingsLayout L(g.w, g.h);
  const Rect& hd = L.header;
  g.text(geist_title, hd.x, hd.y + 36, "Settings");
  const char* conn = st.connected ? "Connected" : (st.pairing ? "Pairing" : "Not connected");
  drawBadge(g, hd.x, hd.y + 52, conn, st.connected);
  if (st.battery >= 0) {
    char pct[8];
    int p = st.battery > 100 ? 100 : st.battery;
    pct[0] = 0;
    { int n = 0; if (p >= 100) pct[n++] = '1'; if (p >= 10) pct[n++] = char('0' + (p / 10) % 10);
      pct[n++] = char('0' + p % 10); pct[n++] = '%'; pct[n] = 0; }
    int right = hd.x + hd.w;
    icon::battery(g, right - 46, hd.y + 60, 46, 22, p, st.charging);
    g.textRight(geist_sm, right - 54, Gfx::capBaseline(geist_sm, hd.y + 52, 36), pct);
  }
  const uint8_t sel[SEG_COUNT] = {s.mode, s.side, s.speed, s.rotation};
  for (int i = 0; i < SEG_COUNT; ++i) drawSegmented(g, L.seg[i], SEG_TITLE[i], SEG_LABELS[i], SEG_N[i], sel[i]);
  const Rect& so = L.sound;
  g.text(geist_med, so.x + 2, Gfx::capBaseline(geist_med, so.y, so.h), "Click sound");
  drawSwitch(g, so.x + so.w - 78, so.y + (so.h - 42) / 2, s.sound);
  drawButton(g, L.forget, forgetArmed ? "Confirm" : "Forget host", forgetArmed);
  drawButton(g, L.done, "Done", true);
}

}  // namespace app
