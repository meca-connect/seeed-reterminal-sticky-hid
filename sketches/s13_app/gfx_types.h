#pragma once
#include <stdint.h>
// Bitmap font format produced by scripts/gen_font.py.
struct StickyGlyph {
  uint32_t off;      // bit offset into bits[]
  uint8_t w, h;      // ink box
  uint8_t adv;       // advance width
  int8_t xo, yo;     // ink box offset from pen position on the baseline
};
struct StickyFont {
  const uint8_t* bits;
  const StickyGlyph* glyphs;
  uint8_t first, last;
  uint8_t ascent, descent;
};
