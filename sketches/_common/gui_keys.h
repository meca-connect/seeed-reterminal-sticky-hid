/*
 * Key grids + rectangle helpers for the Sticky GUI (numpad + keyboard).
 * Portrait 480x800 logical space; draws via stickyDrawPixel. Include after
 * ../_common/sticky_hw.h and ../_common/font5x7.h.
 */
#pragma once
#include "font5x7.h"

struct Key { const char* label; uint8_t usage; };   // usage 0xFF = shift toggle

static const Key NUMPAD[12] = {
  {"7",0x24},{"8",0x25},{"9",0x26},
  {"4",0x21},{"5",0x22},{"6",0x23},
  {"1",0x1E},{"2",0x1F},{"3",0x20},
  {"<",0x2A},{"0",0x27},{"OK",0x28},
};

static const Key KEYBOARD[50] = {
  {"1",0x1E},{"2",0x1F},{"3",0x20},{"4",0x21},{"5",0x22},{"6",0x23},{"7",0x24},{"8",0x25},{"9",0x26},{"0",0x27},
  {"q",0x14},{"w",0x1A},{"e",0x08},{"r",0x15},{"t",0x17},{"y",0x1C},{"u",0x18},{"i",0x0C},{"o",0x12},{"p",0x13},
  {"a",0x04},{"s",0x16},{"d",0x07},{"f",0x09},{"g",0x0A},{"h",0x0B},{"j",0x0D},{"k",0x0E},{"l",0x0F},{"<",0x2A},
  {"^",0xFF},{"z",0x1D},{"x",0x1B},{"c",0x06},{"v",0x19},{"b",0x05},{"n",0x11},{"m",0x10},{",",0x36},{".",0x37},
  {"TAB",0x2B},{" ",0x2C},{" ",0x2C},{" ",0x2C},{" ",0x2C},{" ",0x2C},{" ",0x2C},{" ",0x2C},{" ",0x2C},{"OK",0x28},
};

static const int GUI_HEADER = 46;   // title strip
static const int GUI_FOOTER = 30;   // status/hint strip
static const int GUI_MARGIN = 8;
static const int GUI_GAP = 4;

static void fillRect(int x, int y, int w, int h, bool black) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx) stickyDrawPixel(xx, yy, black);
}
static void drawRect(int x, int y, int w, int h, bool black) {
  for (int xx = x; xx < x + w; ++xx) { stickyDrawPixel(xx, y, black); stickyDrawPixel(xx, y + h - 1, black); }
  for (int yy = y; yy < y + h; ++yy) { stickyDrawPixel(x, yy, black); stickyDrawPixel(x + w - 1, yy, black); }
}

static void drawKey(int x, int y, int w, int h, const char* label, int scale) {
  drawRect(x, y, w, h, true);
  drawTextCentered(x + w / 2, y + (h - 7 * scale) / 2, label, scale, true);
}

// Cell size and grid origin for a cols x rows layout in the content area
// below the header and above the footer.
static void gridLayout(int cols, int rows, int* cellW, int* cellH, int* gridX, int* gridY) {
  int contentH = 800 - GUI_HEADER - GUI_FOOTER;
  *cellW = (480 - GUI_MARGIN * 2 - GUI_GAP * (cols - 1)) / cols;
  *cellH = (contentH - GUI_MARGIN * 2 - GUI_GAP * (rows - 1)) / rows;
  *gridX = GUI_MARGIN;
  *gridY = GUI_HEADER + GUI_MARGIN;
}

// Returns the key index for a logical tap point, or -1 if in a gap/outside.
static int keyIndexAt(int tx, int ty, int cols, int rows, int cellW, int cellH, int gridX, int gridY) {
  if (tx < gridX || ty < gridY) return -1;
  int c = (tx - gridX) / (cellW + GUI_GAP);
  int r = (ty - gridY) / (cellH + GUI_GAP);
  if (c < 0 || c >= cols || r < 0 || r >= rows) return -1;
  if ((tx - gridX) % (cellW + GUI_GAP) >= cellW) return -1;
  if ((ty - gridY) % (cellH + GUI_GAP) >= cellH) return -1;
  return r * cols + c;
}
