#include "StickyDisplay.h"
#include "UIModel.h"
#include <string.h>

namespace sticky_display_detail {
void Frame::clear() {
  if (valid()) memset(bytes_, 0xFF, kFrameBytes);
}
void Frame::mirrorX() {
  if (!valid()) return;
  // Derive the MSB-first horizontal reflection without a second framebuffer.
  const auto reverse = [](uint8_t v) {
    v = static_cast<uint8_t>(((v & 0x55U) << 1) | ((v & 0xAAU) >> 1));
    v = static_cast<uint8_t>(((v & 0x33U) << 2) | ((v & 0xCCU) >> 2));
    return static_cast<uint8_t>((v << 4) | (v >> 4));
  };
  for (int y = 0; y < kHeight; ++y) {
    uint8_t* row = bytes_ + static_cast<size_t>(y) * kStride;
    for (size_t x = 0; x < kStride / 2; ++x) {
      const uint8_t left = row[x];
      row[x] = reverse(row[kStride - 1 - x]);
      row[kStride - 1 - x] = reverse(left);
    }
  }
}
bool Frame::pixel(int x, int y, bool white) {
  if (!valid() || x < 0 || x >= kWidth || y < 0 || y >= kHeight) return false;
  uint8_t& byte = bytes_[static_cast<size_t>(y) * kStride + (x >> 3)];
  const uint8_t mask = 0x80U >> (x & 7);
  if (white) byte |= mask;
  else byte &= static_cast<uint8_t>(~mask);
  return true;
}
// Small, original fixed 5x7 uppercase font; no graphics library/hardware probing.
namespace {
struct Glyph { char ch; uint8_t rows[7]; };
constexpr Glyph kFont[] = {
 {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
 {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
 {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
 {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}},
 {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
 {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
 {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
 {' ',{0,0,0,0,0,0,0}}, {'-',{0,0,0,31,0,0,0}},
 {'+',{0,4,4,31,4,4,0}}, {'=',{0,0,31,0,31,0,0}},
 {'.',{0,0,0,0,0,12,12}}, {':',{0,12,12,0,12,12,0}},
 {'/',{1,1,2,4,8,16,16}}, {'<',{2,4,8,16,8,4,2}},
 {'>',{8,4,2,1,2,4,8}}, {'[',{14,8,8,8,8,8,14}},
 {']',{14,2,2,2,2,2,14}}, {'(',{2,4,8,8,8,4,2}},
 {')',{8,4,2,2,2,4,8}}, {'!',{4,4,4,4,4,0,4}},
 {'?',{14,17,1,2,4,0,4}}, {'*',{0,21,14,31,14,21,0}},
 {'_', {0,0,0,0,0,0,31}}, {',',{0,0,0,0,4,4,8}},
 {';', {0,4,4,0,4,4,8}}, {'\'',{4,4,8,0,0,0,0}},
 {'"',{10,10,20,0,0,0,0}}, {'@',{14,17,23,21,23,16,14}},
 {'#',{10,10,31,10,31,10,10}}, {'$',{4,15,20,14,5,30,4}},
 {'%',{17,2,4,4,8,16,17}}, {'^',{4,10,17,0,0,0,0}},
 {'&',{12,18,20,8,21,18,13}}, {'|',{4,4,4,4,4,4,4}},
 {'\\',{16,16,8,4,2,1,1}}, {'`',{8,4,2,0,0,0,0}},
 {'~',{0,0,9,22,0,0,0}}, {'{',{2,4,4,8,4,4,2}},
 {'}',{8,4,4,2,4,4,8}}
};
const uint8_t* glyph(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
  for (const auto& g : kFont) if (g.ch == ch) return g.rows;
  for (const auto& g : kFont) if (g.ch == '?') return g.rows;
  return kFont[0].rows;
}
}  // namespace
void Frame::fillRect(int x, int y, int w, int h, bool white) {
  if (!valid() || w <= 0 || h <= 0) return;
  const int64_t right = static_cast<int64_t>(x) + w;
  const int64_t bottom = static_cast<int64_t>(y) + h;
  const int x0 = x < 0 ? 0 : x;
  const int y0 = y < 0 ? 0 : y;
  const int x1 = right > kWidth ? kWidth : static_cast<int>(right);
  const int y1 = bottom > kHeight ? kHeight : static_cast<int>(bottom);
  for (int yy = y0; yy < y1; ++yy)
    for (int xx = x0; xx < x1; ++xx) pixel(xx, yy, white);
}
void Frame::rect(int x, int y, int w, int h, bool white) {
  if (w <= 0 || h <= 0 || w > kWidth * 2 || h > kHeight * 2) return;
  // Renderer supplies bounded coordinates; fillRect clips each edge.
  if (x < -kWidth || x > kWidth || y < -kHeight || y > kHeight) return;
  fillRect(x, y, w, 1, white);
  fillRect(x, y + h - 1, w, 1, white);
  fillRect(x, y, 1, h, white);
  fillRect(x + w - 1, y, 1, h, white);
}
int Frame::textWidth(const char* value, unsigned scale) {
  if (!value || !*value || !scale || scale > 8) return 0;
  size_t n = strlen(value);
  if (n > 256) n = 256;
  return static_cast<int>((n * 6 - 1) * scale);
}
void Frame::text(int x, int y, const char* value, unsigned scale, bool white) {
  if (!valid() || !value || !scale || scale > 8) return;
  if (x < -kWidth || x >= kWidth || y < -kHeight || y >= kHeight) return;
  for (size_t n = 0; value[n] && n < 256 && x < kWidth; ++n, x += 6 * scale) {
    const uint8_t* rows = glyph(value[n]);
    for (int yy = 0; yy < 7; ++yy)
      for (int xx = 0; xx < 5; ++xx)
        if (rows[yy] & (16U >> xx))
          fillRect(x + xx * scale, y + yy * scale, scale, scale, white);
  }
}
bool render(Frame& f, const DrawState& state) {
  if (!f.valid() || state.screen > 3) return false;
  f.clear();
  static const char* const titles[] = {"TRACKPAD", "NUMPAD", "KEYBOARD", "SETTINGS"};
  const char* title = titles[state.screen];
  f.fillRect(0, 0, kWidth, 48, false);
  f.text(18, 12, title, 3, true);
  f.text(320, 12, state.armed ? "ARMED" : "PAUSED", 3, true);
  f.text(508, 16, state.connected ? "BLUETOOTH CONNECTED" : "BLUETOOTH NOT CONNECTED", 2, true);
  f.fillRect(0, 434, kWidth, 2, false);
  f.text(18, 448, "AI: PAUSE/ARM    UP/DOWN: PAGES", 2);
  char rot[] = "TOUCH ROT: 0";
  rot[11] = static_cast<char>('0' + (state.rotation & 3));
  f.text(638, 448, rot, 2);
  if (state.screen == 0) {
    f.text(24, 62, "1F MOVE/TAP/DRAG   2F SCROLL/TAP RIGHT   PINCH CTRL+WHEEL", 2);
    f.rect(18, 90, 754, 326, false);
    for (int y = 110; y < 410; y += 24)
      for (int x = 38; x < 762; x += 24) f.pixel(x, y, false);
    // Match GestureEngine's 20-pixel right-edge latch (x >= 780).
    f.fillRect(780, 90, 20, 326, false);
    for (int i = 0; i < 6; ++i) {
      f.fillRect(790 - i, 109 + i, 1, 1, true);
      f.fillRect(790 + i, 109 + i, 1, 1, true);
      f.fillRect(790 - i, 397 - i, 1, 1, true);
      f.fillRect(790 + i, 397 - i, 1, 1, true);
    }
    for (int i = 0; i < 6; ++i) {
      const char c[] = {"SCROLL"[i], 0};
      f.text(785, 158 + i * 24, c, 2, true);
    }
    // An opaque legend avoids dots running through readable instructions.
    f.fillRect(36, 296, 680, 100, true);
    f.text(48, 306, "3F TAP: MIDDLE CLICK", 3);
    f.text(48, 341, "3F LEFT/RIGHT: ALT+LEFT/RIGHT", 2);
    f.text(48, 369, "3F UP: GUI+TAB    DOWN: GUI+D", 2);
  } else if (state.screen == 1 || state.screen == 2) {
    const auto screen = static_cast<stickyapp::Screen>(state.screen);
    f.text(18, 62, state.screen == 1 ? "TAP A KEY TO TYPE" :
           state.shifted ? "SHIFT ON - NEXT KEY ONLY" : "TAP SHIFT FOR SYMBOLS / UPPERCASE", 2);
    for (int i = 0; i < stickyapp::keyCount(screen); ++i) {
      const auto key = stickyapp::keyAt(screen, i, state.shifted);
      const bool selected = key.usage == 0xFF && state.shifted;
      f.fillRect(key.x, key.y, key.w, key.h, !selected);
      f.rect(key.x, key.y, key.w, key.h, false);
      unsigned scale = state.screen == 1 ? 4 : 2;
      while (scale > 1 && Frame::textWidth(key.label, scale) > key.w - 8) --scale;
      const int x = key.x + (key.w - Frame::textWidth(key.label, scale)) / 2;
      const int y = key.y + (key.h - 7 * static_cast<int>(scale)) / 2;
      f.text(x, y, key.label, scale, selected);
    }
  } else {
    f.text(24, 72, "CONTROLS", 3);
    f.text(24, 117, "AI BUTTON: TOGGLE PAUSED / ARMED", 2);
    f.text(24, 153, "UP / DOWN: TRACKPAD - NUMPAD - KEYBOARD - SETTINGS", 2);
    f.text(24, 200, "ORIENTATION IS PROVISIONAL - TEST THE FOUR CORNERS", 2);
    f.text(24, 232, "SERIAL: ROTATE 0..3  (TOUCH MAPPING; UI STAYS LANDSCAPE)", 2);
    f.text(24, 278, "GENERIC BLUETOOTH MOUSE + KEYBOARD", 2);
    f.text(24, 310, "STOCK USB-C: UART ONLY - NO NATIVE USB HID", 2);
    f.text(24, 356, "FORGET HOST: SERIAL FORGET, THEN FORGET YES IN 5S", 2);
    f.text(24, 388, "NO SLEEP. REFRESH ONLY WHEN INPUT IS IDLE.", 2);
  }
  return true;
}
}  // namespace sticky_display_detail

#ifdef ARDUINO
#include <Arduino.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <new>

// Register values and sequence checked against Seeed Studio's official
// https://files.seeedstudio.com/wiki/reterminal_sticky/res/Sticky_peripheral_demo.zip
// main/demo_epaper.cpp, components/seeed_epaper/driver/ssd1677.c and epaper_panel.c.
// Original archive retained at reference/Sticky_peripheral_demo.zip. The archive
// supplies no SSD1677 component license (button/license.txt is for another
// component). This is an independently written small register-protocol adapter;
// no vendor source file, font, or ESP-IDF component is vendored/relicensed here.
// Full refresh only: both RAM planes receive identical data, update 0xF7.
// SPI2 is exclusively owned here; SD is not mounted and CS8 stays deselected.
namespace {
constexpr int kEnable = 47, kReset = 17, kBusy = 18, kCS = 15, kDC = 16;
constexpr int kMOSI = 14, kSCK = 13, kSDCS = 8;
constexpr uint32_t kBusyTimeout = 10000;
constexpr size_t kChunk = 1024;
}  // namespace

struct StickyDisplay::State {
  uint8_t* pixels = nullptr;
  uint8_t* tx = nullptr;  // DMA/internal staging; framebuffer can be in PSRAM.
  spi_device_handle_t spi = nullptr;
  spi_transaction_t transaction = {};
  bool ownsBus = false;
  bool inFlight = false;
  bool ready = false;
  bool displayed = false;
  sticky_display_detail::DrawState last = {};

  bool fail(const char* where) {
    ready = displayed = false;
    Serial.printf("[DISPLAY] FAILED: %s; call begin() to retry\n", where);
    return false;
  }
  bool waitReady(bool requireBusy = false) {
    const uint32_t start = millis();
    if (requireBusy) {
      // BUSY should assert immediately after master activation. Requiring it
      // prevents a disconnected/stuck-low BUSY input masquerading as refresh.
      while (digitalRead(kBusy) != HIGH) {
        if (static_cast<uint32_t>(millis() - start) >= 250)
          return fail("refresh BUSY never asserted");
        delay(1);
      }
    }
    while (digitalRead(kBusy) == HIGH) {
      if (static_cast<uint32_t>(millis() - start) >= kBusyTimeout)
        return fail("BUSY timeout (10 s)");
      delay(1);
    }
    return true;
  }
  bool send(bool data, const uint8_t* bytes, size_t length) {
    if (!spi || !tx || inFlight || (!bytes && length)) return fail("SPI not ready");
    digitalWrite(kSDCS, HIGH);
    digitalWrite(kDC, data ? HIGH : LOW);
    while (length) {
      const size_t n = length > kChunk ? kChunk : length;
      memcpy(tx, bytes, n);
      transaction = {};
      transaction.length = n * 8;
      transaction.tx_buffer = tx;
      esp_err_t err = spi_device_queue_trans(spi, &transaction, pdMS_TO_TICKS(100));
      if (err != ESP_OK) return fail("SPI queue error");
      inFlight = true;
      spi_transaction_t* completed = nullptr;
      err = spi_device_get_trans_result(spi, &completed, pdMS_TO_TICKS(1000));
      // On timeout, leave the transaction and DMA buffer alive and untouched.
      // begin() can retry only after the queued transfer is drained safely.
      if (err != ESP_OK) return fail("SPI completion timeout/error");
      inFlight = false;
      if (completed != &transaction) return fail("SPI transaction mismatch");
      bytes += n;
      length -= n;
    }
    return true;
  }
  bool command(uint8_t value) { return send(false, &value, 1); }
  bool reg(uint8_t cmd, const uint8_t* values, size_t len) {
    return command(cmd) && send(true, values, len);
  }
  bool reg8(uint8_t cmd, uint8_t value) { return reg(cmd, &value, 1); }
  bool initPanel() {
    digitalWrite(kReset, LOW); delay(10);
    digitalWrite(kReset, HIGH); delay(10);
    if (!waitReady() || !command(0x12) || !waitReady()) return false;
    const uint8_t softStart[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    const uint8_t driverOutput[] = {0xDF, 0x01, 0x02};  // 479
    const uint8_t xWindow[] = {0, 0, 0x1F, 0x03};      // 0..799 (pixels, not bytes)
    const uint8_t yWindow[] = {0, 0, 0xDF, 0x01};      // 0..479
    const uint8_t zero[] = {0, 0};
    return reg8(0x18, 0x80) && reg8(0x3C, 0x01) &&
           reg(0x0C, softStart, sizeof(softStart)) &&
           reg(0x01, driverOutput, sizeof(driverOutput)) && reg8(0x11, 0x03) &&
           reg(0x44, xWindow, sizeof(xWindow)) && reg(0x45, yWindow, sizeof(yWindow)) &&
           reg(0x4E, zero, sizeof(zero)) && reg(0x4F, zero, sizeof(zero)) && waitReady();
  }
};

bool StickyDisplay::begin() {
  if (!state_) state_ = new (std::nothrow) State;
  if (!state_) return false;
  State& s = *state_;
  if (s.ready) return true;
  if (s.inFlight) {
    spi_transaction_t* done = nullptr;
    if (spi_device_get_trans_result(s.spi, &done, 0) != ESP_OK)
      return s.fail("previous SPI transfer still pending");
    s.inFlight = false;
    if (done != &s.transaction) return s.fail("previous SPI result mismatch");
  }
  if (!s.pixels) {
    s.pixels = static_cast<uint8_t*>(heap_caps_malloc(sticky_display_detail::kFrameBytes,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s.pixels) s.pixels = static_cast<uint8_t*>(heap_caps_malloc(
                       sticky_display_detail::kFrameBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (!s.tx) s.tx = static_cast<uint8_t*>(heap_caps_malloc(
                                    kChunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (!s.pixels || !s.tx) return s.fail("framebuffer/DMA allocation");
  pinMode(kSDCS, OUTPUT); digitalWrite(kSDCS, HIGH);
  pinMode(kCS, OUTPUT); digitalWrite(kCS, HIGH);
  pinMode(kDC, OUTPUT); digitalWrite(kDC, LOW);
  pinMode(kReset, OUTPUT); digitalWrite(kReset, HIGH);
  pinMode(kBusy, INPUT);
  pinMode(kEnable, OUTPUT); digitalWrite(kEnable, HIGH);
  delay(100);
  if (!s.ownsBus) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = kMOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = kSCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = kChunk;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK)
      return s.fail("SPI2 init (must be exclusively owned)");
    s.ownsBus = true;
  }
  if (!s.spi) {
    spi_device_interface_config_t device = {};
    device.clock_speed_hz = 10000000;
    device.mode = 0;
    device.spics_io_num = kCS;
    device.queue_size = 1;
    if (spi_bus_add_device(SPI2_HOST, &device, &s.spi) != ESP_OK)
      return s.fail("SPI device add");
  }
  s.displayed = false;
  s.ready = s.initPanel();
  if (s.ready) Serial.println("[DISPLAY] SSD1677 configured; physical orientation unverified");
  return s.ready;
}
bool StickyDisplay::available() const { return state_ && state_->ready; }
bool StickyDisplay::draw(uint8_t screen, bool armed, bool connected, uint8_t rotation, bool shifted) {
  if (screen > 3 || !available()) return false;
  State& s = *state_;
  const sticky_display_detail::DrawState current{screen, armed, connected,
                                              static_cast<uint8_t>(rotation & 3), shifted};
  if (s.displayed && !sticky_display_detail::needsRefresh(s.last, current)) return true;
  sticky_display_detail::Frame frame(s.pixels, sticky_display_detail::kFrameBytes);
  if (!sticky_display_detail::render(frame, current)) return false;
  // Match Seeed's mirror_x=true: reverse row bytes AND bits. No Y reflection.
  // rotation changes only the app's raw-touch transform, not this 800x480 raster.
  frame.mirrorX();
  if (!s.waitReady() || !s.initPanel() ||
      !s.reg(0x26, s.pixels, sticky_display_detail::kFrameBytes) ||
      !s.reg(0x24, s.pixels, sticky_display_detail::kFrameBytes) ||
      !s.reg8(0x22, 0xF7) || !s.command(0x20) || !s.waitReady(true)) return false;
  s.last = current;
  s.displayed = true;
  return true;
}
#endif  // ARDUINO
