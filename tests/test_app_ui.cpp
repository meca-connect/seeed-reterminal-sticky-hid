// Host tests + PNG previews for the s13 app UI.
// g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Isketches/s13_app
//     tests/test_app_ui.cpp -o build/test_app_ui && build/test_app_ui
// Writes build/previews/*.pbm (convert: python3 scripts/previews_to_png.py).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "app_ui.h"
using namespace app;

struct Frame {
  int w, h; std::vector<uint8_t> px;
  Frame(int W, int H) : w(W), h(H), px(size_t(W) * H, 0) {}
  static void sink(void* c, int x, int y, bool b) { auto* f = static_cast<Frame*>(c); f->px[size_t(y) * f->w + x] = b; }
  Gfx gfx() { Gfx g; g.w = w; g.h = h; g.sink = sink; g.ctx = this; return g; }
  void save(const std::string& name) const {
    mkdir("build", 0755); mkdir("build/previews", 0755);
    FILE* f = fopen(("build/previews/" + name + ".pbm").c_str(), "wb");
    assert(f);
    fprintf(f, "P4\n%d %d\n", w, h);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; x += 8) {
        uint8_t b = 0;
        for (int i = 0; i < 8 && x + i < w; ++i) if (px[size_t(y) * w + x + i]) b |= 0x80 >> i;
        fputc(b, f);
      }
    fclose(f);
  }
};

static void keymap() {
  uint8_t u; bool sh;
  assert(asciiToHid('a', u, sh) && u == 0x04 && !sh);
  assert(asciiToHid('Z', u, sh) && u == 0x1D && sh);
  assert(asciiToHid('0', u, sh) && u == 0x27 && !sh);
  assert(asciiToHid('!', u, sh) && u == 0x1E && sh);
  assert(asciiToHid('?', u, sh) && u == 0x38 && sh);
  assert(asciiToHid(' ', u, sh) && u == 0x2C);
  assert(asciiToHid('"', u, sh) && u == 0x34 && sh);
  assert(!asciiToHid('\t', u, sh));
  for (const KeyLayout* kl : {&LAYOUT_LETTERS, &LAYOUT_SYMBOLS, &LAYOUT_NUMPAD})
    for (int i = 0; i < kl->count; ++i)
      if (kl->keys[i].act == ACT_CHAR) assert(asciiToHid(kl->keys[i].ch, u, sh));
  puts("PASS every on-screen character maps to a US HID usage");
}

static void layouts() {
  for (int rot = 0; rot < 4; ++rot) {
    int W = rot & 1 ? 800 : 480, H = rot & 1 ? 480 : 800;
    KbState kb;
    for (uint8_t mode = 0; mode < MODE_COUNT; ++mode) {
      MainLayout L(W, H, mode, kb);
      if (mode == MODE_TRACKPAD) { assert(L.pad.h > 600 || L.pad.h > 350); continue; }
      assert(L.keys.y >= HANDLE_H && L.keys.y + L.keys.h <= H);
      if (!(rot & 1)) assert(!L.pad.empty() && L.pad.h >= 180);   // portrait keeps a pad
      for (int i = 0; i < L.kl->count; ++i) {
        Rect r = L.keyRect(L.kl->keys[i]);
        assert(r.w >= 36 && r.h >= 50);
        assert(L.keyAt(r.x + r.w / 2, r.y + r.h / 2) == i);
        for (int j = 0; j < i; ++j) {   // no overlaps
          Rect o = L.keyRect(L.kl->keys[j]);
          assert(r.x >= o.x + o.w || o.x >= r.x + r.w || r.y >= o.y + o.h || o.y >= r.y + r.h);
        }
      }
    }
    SettingsLayout S(W, H);
    const Rect* all[] = {&S.seg[0], &S.seg[1], &S.seg[2], &S.seg[3], &S.sound, &S.forget, &S.done};
    for (const Rect* r : all) assert(r->x >= 0 && r->y >= 0 && r->x + r->w <= W && r->y + r->h <= H);
    for (int a = 0; a < 7; ++a) for (int b = 0; b < a; ++b) {
      const Rect &p = *all[a], &q = *all[b];
      assert(p.x >= q.x + q.w || q.x >= p.x + p.w || p.y >= q.y + q.h || q.y >= p.y + p.h);
    }
    for (int s = 0; s < SEG_COUNT; ++s)
      for (int i = 0; i < SEG_N[s]; ++i) {
        const Rect& r = S.seg[s];
        Event e = S.hit(r.x + r.w * (2 * i + 1) / (2 * SEG_N[s]), r.y + r.h / 2);
        assert(e.type == EV_MODE + s && e.index == i);
      }
    assert(S.hit(S.done.x + 5, S.done.y + 5).type == EV_DONE);
    assert(S.hit(S.forget.x + 5, S.forget.y + 5).type == EV_FORGET);
    assert(S.hit(S.sound.x + 5, S.sound.y + 5).type == EV_SOUND);
  }
  puts("PASS main and settings layouts fit, don't overlap and hit-test in all rotations");
}

static void previews() {
  Settings s; Status st; st.connected = true; st.battery = 82; KbState kb;
  for (int rot = 0; rot < 2; ++rot) {
    int W = rot ? 800 : 480, H = rot ? 480 : 800;
    std::string sfx = rot ? "_landscape" : "";
    Frame f(W, H); Gfx g = f.gfx();
    s.mode = MODE_TRACKPAD; drawMain(g, s, st, kb); f.save("trackpad" + sfx);
    s.mode = MODE_KEYBOARD; drawMain(g, s, st, kb); f.save("keyboard" + sfx);
    kb.shift = SHIFT_ONCE; drawMain(g, s, st, kb); f.save("keyboard_shift" + sfx); kb.shift = SHIFT_OFF;
    kb.layer = LAYER_SYMBOLS; drawMain(g, s, st, kb); f.save("keyboard_symbols" + sfx); kb.layer = LAYER_LETTERS;
    s.mode = MODE_NUMPAD; drawMain(g, s, st, kb); f.save("numpad" + sfx);
    s.mode = MODE_TRACKPAD;
    drawSettings(g, s, st, false); f.save("settings" + sfx);
    drawSleep(g, st, true); f.save("sleep" + sfx);
  }
  Frame f(480, 800); Gfx g = f.gfx();
  Status off; off.pairing = true; off.battery = 9;
  s.side = SIDE_BOTH; drawMain(g, s, off, kb); f.save("trackpad_pairing_lowbat_both");
  s.sound = false; drawSettings(g, s, off, true); f.save("settings_pairing_armed");
  puts("PASS previews rendered to build/previews");
}

int main() {
  keymap();
  layouts();
  previews();
  puts("ALL APP UI TESTS PASSED");
}
