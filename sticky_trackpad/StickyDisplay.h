#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure 1-bit raster helpers also used by the host tests. White=1, MSB first.
namespace sticky_display_detail {
constexpr int kWidth = 800;
constexpr int kHeight = 480;
constexpr size_t kStride = kWidth / 8;
constexpr size_t kFrameBytes = kStride * kHeight;
class Frame {
 public:
  Frame(uint8_t* bytes, size_t length) : bytes_(bytes), length_(length) {}
  bool valid() const { return bytes_ != nullptr && length_ >= kFrameBytes; }
  void clear();
  void mirrorX();
  bool pixel(int x, int y, bool white);
  void fillRect(int x, int y, int w, int h, bool white);
  void rect(int x, int y, int w, int h, bool white);
  void text(int x, int y, const char* value, unsigned scale = 2, bool white = false);
  static int textWidth(const char* value, unsigned scale = 2);
 private:
  uint8_t* bytes_;
  size_t length_;
};
struct DrawState {
  uint8_t screen;
  bool armed;
  bool connected;
  uint8_t rotation;
  bool shifted;
};
inline bool needsRefresh(const DrawState& a, const DrawState& b) {
  return a.screen != b.screen || a.armed != b.armed || a.connected != b.connected ||
         (a.rotation & 3) != (b.rotation & 3) ||
         (b.screen == 2 && a.shifted != b.shifted);
}
bool render(Frame& frame, const DrawState& state);
}  // namespace sticky_display_detail

// SSD1677 Sticky display. Intended as one long-lived instance.
// begin(): allocate/configure, reset and wait ready (no panel-ID readback exists).
// draw(): full refresh on a changed UI state; identical requests are no-ops.
// A successful changed draw requires a BUSY assertion followed by ready, not
// merely successful SPI submission. That is not an optical display verification.
// The caller MUST release HID state and ensure no active touches before draw().
// UI remains 800x480 landscape; rotation reports the app's touch-axis mapping.
// No panel/board sleep or native-USB HID implementation is present here.
class StickyDisplay {
 public:
  StickyDisplay() = default;
  StickyDisplay(const StickyDisplay&) = delete;
  StickyDisplay& operator=(const StickyDisplay&) = delete;
  bool begin();
  bool draw(uint8_t screen, bool armed, bool connected, uint8_t rotation,
            bool shifted = false);
  bool available() const;
 private:
  struct State;
  State* state_ = nullptr;
};
