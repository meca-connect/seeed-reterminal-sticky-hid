#pragma once
// Portable 1-bit drawing layer (no Arduino dependency) so every screen can be
// rendered on the host for review (tests/test_app_ui.cpp -> build/previews).
// The firmware binds Gfx::px to the rotated e-paper framebuffer.
#include <stdint.h>
#include <string.h>
#include "gfx_types.h"
#include "fonts.h"

enum Fill : uint8_t { INK, PAPER, GRAY50, GRAY25, GRAY12 };

struct Gfx {
  int w = 0, h = 0;
  void (*sink)(void* ctx, int x, int y, bool black) = nullptr;
  void* ctx = nullptr;
  void (*clearSink)(void* ctx) = nullptr;   // optional fast clear to paper

  inline void px(int x, int y, bool black) const {
    if (x >= 0 && y >= 0 && x < w && y < h) sink(ctx, x, y, black);
  }
  // Ordered patterns read as greys on a ~235 dpi panel.
  static bool ink(Fill f, int x, int y) {
    switch (f) {
      case INK: return true;
      case PAPER: return false;
      case GRAY50: return ((x ^ y) & 1) == 0;
      case GRAY25: return (x & 1) == 0 && (y & 1) == 0;
      default: return (x & 3) == 0 && (y & 3) == 0;
    }
  }
  void clear() const { if (clearSink) clearSink(ctx); else fill(0, 0, w, h, PAPER); }
  void fill(int x, int y, int fw, int fh, Fill f) const {
    for (int yy = y; yy < y + fh; ++yy)
      for (int xx = x; xx < x + fw; ++xx) px(xx, yy, ink(f, xx, yy));
  }
  void hline(int x, int y, int len, int t = 1, Fill f = INK) const { fill(x, y, len, t, f); }
  void vline(int x, int y, int len, int t = 1, Fill f = INK) const { fill(x, y, t, len, f); }

  // Is (x,y) inside a w x h rounded rect at (0,0) with radius r?
  static bool inRound(int x, int y, int rw, int rh, int r) {
    if (x < 0 || y < 0 || x >= rw || y >= rh) return false;
    int cx = x < r ? r : (x >= rw - r ? rw - r - 1 : x);
    int cy = y < r ? r : (y >= rh - r ? rh - r - 1 : y);
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
  }
  static int isqrt(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) ++r; return r; }
  // Horizontal extent [x0, x1) of row yy inside a rw x rh rounded rect.
  static void roundSpan(int yy, int rw, int rh, int r, int& x0, int& x1) {
    int dy = yy < r ? r - yy : (yy >= rh - r ? yy - (rh - r - 1) : 0);
    int in = dy ? r - isqrt(r * r - dy * dy) : 0;
    x0 = in; x1 = rw - in;
  }
  void span(int y, int x0, int x1, Fill f) const {
    for (int x = x0; x < x1; ++x) px(x, y, ink(f, x, y));
  }
  void fillRound(int x, int y, int rw, int rh, int r, Fill f = INK) const {
    for (int yy = 0; yy < rh; ++yy) { int a, b; roundSpan(yy, rw, rh, r, a, b); span(y + yy, x + a, x + b, f); }
  }
  void strokeRound(int x, int y, int rw, int rh, int r, int t = 2, Fill f = INK) const {
    int ir = r > t ? r - t : 0, iw = rw - 2 * t, ih = rh - 2 * t;
    for (int yy = 0; yy < rh; ++yy) {
      int a, b; roundSpan(yy, rw, rh, r, a, b);
      int iy = yy - t;
      if (iy < 0 || iy >= ih || iw <= 0) { span(y + yy, x + a, x + b, f); continue; }
      int c, d; roundSpan(iy, iw, ih, ir, c, d);
      span(y + yy, x + a, x + t + c, f);
      span(y + yy, x + t + d, x + b, f);
    }
  }
  void disc(int cx, int cy, int r, Fill f = INK) const {
    for (int y = -r; y <= r; ++y)
      for (int x = -r; x <= r; ++x)
        if (x * x + y * y <= r * r) px(cx + x, cy + y, ink(f, cx + x, cy + y));
  }
  void ring(int cx, int cy, int r, int t = 2) const {
    for (int y = -r; y <= r; ++y)
      for (int x = -r; x <= r; ++x) {
        int d = x * x + y * y;
        if (d <= r * r && d > (r - t) * (r - t)) px(cx + x, cy + y, true);
      }
  }
  // Thick line (Bresenham with a square pen).
  void line(int x0, int y0, int x1, int y1, int t = 2, bool black = true) const {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, o = t / 2;
    for (;;) {
      for (int a = 0; a < t; ++a) for (int b = 0; b < t; ++b) px(x0 - o + a, y0 - o + b, black);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  // ---- text ----
  static const StickyGlyph* glyph(const StickyFont& f, char c) {
    uint8_t u = uint8_t(c);
    if (u < f.first || u > f.last) u = '?';
    return &f.glyphs[u - f.first];
  }
  static int textWidth(const StickyFont& f, const char* s) {
    int n = 0;
    for (; *s; ++s) n += glyph(f, *s)->adv;
    return n;
  }
  // Draws with the baseline at y. Returns the pen x after the string.
  int text(const StickyFont& f, int x, int y, const char* s, bool black = true) const {
    for (; *s; ++s) {
      const StickyGlyph* g = glyph(f, *s);
      uint32_t bit = g->off;
      for (int yy = 0; yy < g->h; ++yy)
        for (int xx = 0; xx < g->w; ++xx, ++bit)
          if (f.bits[bit >> 3] & (0x80 >> (bit & 7))) px(x + g->xo + xx, y + g->yo + yy, black);
      x += g->adv;
    }
    return x;
  }
  // Cap-height centred inside a box (uses ascent*0.72 as the cap height).
  static int capBaseline(const StickyFont& f, int y, int bh) { return y + (bh + f.ascent * 72 / 100) / 2; }
  void textIn(const StickyFont& f, int x, int y, int bw, int bh, const char* s, bool black = true) const {
    text(f, x + (bw - textWidth(f, s)) / 2, capBaseline(f, y, bh), s, black);
  }
  void textRight(const StickyFont& f, int right, int y, const char* s, bool black = true) const {
    text(f, right - textWidth(f, s), y, s, black);
  }
};

// ---- icons (stroke style, ~lucide proportions), drawn inside an s x s box ----
namespace icon {
inline void backspace(const Gfx& g, int x, int y, int s, bool k = true) {
  int t = s / 14 + 1, l = x + s / 8, r = x + s - s / 8, top = y + s / 4, bot = y + s - s / 4, mid = y + s / 2;
  int tip = l + (bot - top) / 2;
  g.line(l, mid, tip, top, t, k); g.line(l, mid, tip, bot, t, k);
  g.line(tip, top, r, top, t, k); g.line(tip, bot, r, bot, t, k); g.line(r, top, r, bot, t, k);
  int cx = (tip + r) / 2 + s / 24, d = s / 9;
  g.line(cx - d, mid - d, cx + d, mid + d, t, k); g.line(cx - d, mid + d, cx + d, mid - d, t, k);
}
inline void shift(const Gfx& g, int x, int y, int s, bool filled, bool k = true) {
  int t = s / 14 + 1, cx = x + s / 2, top = y + s / 6, mid = y + s / 2 + s / 12, bot = y + s - s / 6;
  int wide = s / 3, stem = s / 7;
  if (filled) {
    for (int yy = top; yy <= mid; ++yy) {
      int half = (yy - top) * wide / (mid - top);
      g.hline(cx - half, yy, 2 * half + 1, 1, k ? INK : PAPER);
    }
    g.fill(cx - stem, mid, 2 * stem + 1, bot - mid, k ? INK : PAPER);
  } else {
    g.line(cx, top, cx - wide, mid, t, k); g.line(cx, top, cx + wide, mid, t, k);
    g.line(cx - wide, mid, cx - stem, mid, t, k); g.line(cx + wide, mid, cx + stem, mid, t, k);
    g.line(cx - stem, mid, cx - stem, bot, t, k); g.line(cx + stem, mid, cx + stem, bot, t, k);
    g.line(cx - stem, bot, cx + stem, bot, t, k);
  }
}
inline void enter(const Gfx& g, int x, int y, int s, bool k = true) {
  int t = s / 14 + 1, r = x + s - s / 5, l = x + s / 5, top = y + s / 4, mid = y + s / 2 + s / 10, a = s / 7;
  g.line(r, top, r, mid, t, k); g.line(r, mid, l, mid, t, k);
  g.line(l, mid, l + a, mid - a, t, k); g.line(l, mid, l + a, mid + a, t, k);
}
// dir: 0 left, 1 up, 2 right, 3 down
inline void arrow(const Gfx& g, int x, int y, int s, int dir, bool k = true) {
  int t = s / 14 + 1, c = s / 2, a = s / 5, L = s / 3;
  int dx[4] = {-1, 0, 1, 0}, dy[4] = {0, -1, 0, 1};
  int cx = x + c, cy = y + c, hx = cx + dx[dir] * L, hy = cy + dy[dir] * L;
  g.line(cx - dx[dir] * L, cy - dy[dir] * L, hx, hy, t, k);
  int px_ = -dy[dir], py_ = dx[dir];
  g.line(hx, hy, hx - dx[dir] * a + px_ * a, hy - dy[dir] * a + py_ * a, t, k);
  g.line(hx, hy, hx - dx[dir] * a - px_ * a, hy - dy[dir] * a - py_ * a, t, k);
}
inline void check(const Gfx& g, int x, int y, int s, bool k = true) {
  int t = s / 10 + 1;
  g.line(x + s / 5, y + s / 2, x + s * 2 / 5, y + s * 7 / 10, t, k);
  g.line(x + s * 2 / 5, y + s * 7 / 10, x + s * 4 / 5, y + s * 3 / 10, t, k);
}
// Crescent: shown in place of the status dot while the device sleeps.
inline void moon(const Gfx& g, int cx, int cy, int r) {
  g.disc(cx, cy, r);
  g.disc(cx + r / 2, cy - r / 3, r - 2, PAPER);
}
inline void battery(const Gfx& g, int x, int y, int bw, int bh, int pct, bool charging) {
  int t = 2;
  g.strokeRound(x, y, bw - bh / 5, bh, bh / 5, t);
  g.fillRound(x + bw - bh / 5, y + bh / 3, bh / 5, bh / 3, 1);
  int inner = bw - bh / 5 - 4 * t;
  int fillW = pct <= 0 ? 0 : (pct >= 100 ? inner : inner * pct / 100);
  if (fillW < 2 && pct > 0) fillW = 2;
  g.fill(x + 2 * t, y + 2 * t, fillW, bh - 4 * t, INK);
  if (charging) {  // small bolt in paper over the fill
    int cx = x + (bw - bh / 5) / 2, cy = y + bh / 2, s = bh / 3;
    g.line(cx + s / 2, cy - s, cx - s / 3, cy + 1, 2, fillW > (bw / 2) ? false : true);
    g.line(cx - s / 3, cy + 1, cx + s / 3, cy - 1, 2, fillW > (bw / 2) ? false : true);
    g.line(cx + s / 3, cy - 1, cx - s / 2, cy + s, 2, fillW > (bw / 2) ? false : true);
  }
}
}  // namespace icon
