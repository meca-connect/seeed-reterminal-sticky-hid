// Host-only framebuffer tests; these do not establish physical panel operation.
// Pure raster + ASan/UBSan (run from project root):
// g++ -std=c++17 -fsanitize=address,undefined tests/test_display.cpp sticky_trackpad/StickyDisplay.cpp -o "$TMPDIR/display-test"
// Fake SSD1677 transport (never opens a device):
// g++ -std=c++17 -DARDUINO -Itests/fixtures/display tests/test_display.cpp sticky_trackpad/StickyDisplay.cpp -o "$TMPDIR/display-driver-test"
#include <cassert>
#include <cstdio>
#include <vector>
#include "../sticky_trackpad/StickyDisplay.h"
#include "../sticky_trackpad/UIModel.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <driver/spi_master.h>
#endif

int main() {
  using namespace sticky_display_detail;
  // A pixel at either corner must not escape the exact 48,000-byte buffer.
  std::vector<uint8_t> guarded(kFrameBytes + 2, 0xA5);
  Frame frame(guarded.data() + 1, kFrameBytes);
  frame.clear();
  assert(frame.pixel(0, 0, false));
  assert(frame.pixel(799, 479, false));
  assert(!frame.pixel(-1, 0, false));
  assert(!frame.pixel(800, 0, false));
  assert(!frame.pixel(0, 480, false));
  assert(!frame.pixel(0, -1, false));
  assert(guarded.front() == 0xA5 && guarded.back() == 0xA5);
  assert(guarded[1] == 0x7F && guarded[kFrameBytes] == 0xFE);
  assert(frame.pixel(0, 0, true));
  assert(guarded[1] == 0xFF);
  Frame shortFrame(guarded.data() + 1, kFrameBytes - 1);
  assert(!shortFrame.valid());
  assert(!shortFrame.pixel(0, 0, false));
  puts("PASS bounded MSB-first monochrome framebuffer");

  // Full-row X mirroring, including bit order, matches Seeed mirror_x=true.
  frame.clear();
  frame.pixel(0, 0, false);
  frame.pixel(10, 0, false);
  frame.pixel(799, 479, false);
  frame.mirrorX();
  assert(guarded[100] == 0xFE);
  assert(guarded[99] == 0xFB);
  assert(guarded[1 + 479 * kStride] == 0x7F);
  assert(guarded.front() == 0xA5 && guarded.back() == 0xA5);
  frame.mirrorX();
  assert(guarded[1] == 0x7F && guarded[kFrameBytes] == 0xFE);
  puts("PASS vendor-compatible horizontal mirror");

  Frame nullFrame(nullptr, 0);
  nullFrame.clear();
  nullFrame.mirrorX();
  assert(!nullFrame.pixel(0, 0, false));

  // Only discrete UI state changes may request another expensive refresh.
  DrawState last{0, false, false, 0, false};
  assert(!needsRefresh(last, last));
  assert(needsRefresh(last, DrawState{1, false, false, 0, false}));
  assert(needsRefresh(last, DrawState{0, true, false, 0, false}));
  assert(needsRefresh(last, DrawState{0, false, true, 0, false}));
  assert(needsRefresh(last, DrawState{0, false, false, 1, false}));
  assert(needsRefresh(DrawState{2, false, false, 0, false},
                      DrawState{2, false, false, 0, true}));
  assert(!needsRefresh(last, DrawState{0, false, false, 0, true}));
  assert(!needsRefresh(last, DrawState{0, false, false, 4, false}));
  puts("PASS discrete refresh state comparison");

  frame.clear();
  frame.fillRect(-2, -2, 4, 4, false);
  assert(guarded[1] == 0x3F && guarded[101] == 0x3F);
  assert(guarded[201] == 0xFF);
  frame.rect(795, 475, 20, 20, false);
  assert(guarded.front() == 0xA5 && guarded.back() == 0xA5);
  frame.text(10, 10, "A", 1);
  assert(frame.textWidth("ABC", 2) == 34);
  assert(frame.textWidth("", 2) == 0);
  assert(frame.textWidth(nullptr, 2) == 0);
  assert(frame.textWidth("ABC", 0) == 0);
  frame.text(798, 478, "EDGE", 4);
  frame.text(0, 0, nullptr, 2);
  assert(guarded.front() == 0xA5 && guarded.back() == 0xA5);
  puts("PASS clipped shapes and fixed-font metrics");

  // Compare every pixel against an independently reflected coordinate pattern.
  frame.clear();
  for (int y = 0; y < kHeight; ++y)
    for (int x = 0; x < kWidth; ++x)
      if ((x * 17 + y * 13) % 37 == 0) frame.pixel(x, y, false);
  frame.mirrorX();
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const bool white = (guarded[1 + y * kStride + x / 8] & (0x80 >> (x % 8))) != 0;
      assert(white == (((799 - x) * 17 + y * 13) % 37 != 0));
    }
  }
  assert(guarded.front() == 0xA5 && guarded.back() == 0xA5);
  puts("PASS exhaustive pixel reflection");

  assert(render(frame, DrawState{0, false, false, 0, false}));
  assert(guarded[1] == 0x00);  // Black full-width status bar.
  assert(guarded[1 + 100 * kStride + 780 / 8] != 0xFF); // Scroll strip.
  assert(guarded[1 + 100 * kStride + 799 / 8] == 0x00); // Actual right edge.
  assert(guarded[1 + 100 * kStride + 750 / 8] == 0xFF); // Outside 20px scroll strip.
  assert(render(frame, DrawState{3, false, true, 2, false}));
  assert(!render(frame, DrawState{4, false, false, 0, false}));
  assert(!render(nullFrame, DrawState{0, false, false, 0, false}));
  assert(guarded.front() == 0xA5 && guarded.back() == 0xA5);
  puts("PASS bounded trackpad/settings page rasterization");

  for (uint8_t screen = 1; screen <= 2; ++screen) {
    assert(render(frame, DrawState{screen, true, true, 1, false}));
    for (int i = 0; i < stickyapp::keyCount(static_cast<stickyapp::Screen>(screen)); ++i) {
      auto key = stickyapp::keyAt(static_cast<stickyapp::Screen>(screen), i);
      for (int x = key.x; x < key.x + key.w; ++x)
        assert(!(guarded[1 + key.y * kStride + x / 8] & (0x80 >> (x % 8))));
      for (int y = key.y; y < key.y + key.h; ++y)
        assert(!(guarded[1 + y * kStride + key.x / 8] & (0x80 >> (key.x % 8))));
    }
    assert(render(frame, DrawState{screen, true, true, 1, true}));
    assert(guarded.front() == 0xA5 && guarded.back() == 0xA5);
  }
  puts("PASS shared UIModel key-box alignment");
#ifdef ARDUINO
  StickyDisplay display;
  assert(!display.available());
  assert(!display.draw(0, false, false, 0));
  assert(display.begin());
  assert(fakePins[47]==HIGH && fakePins[8]==HIGH);
  assert(display.available());
  assert(display.draw(0, false, false, 0));
  const auto count=sent.size();
  assert(display.draw(0, false, false, 0));
  assert(sent.size()==count);
  size_t planeBytes=0;
  bool refresh=false;
  int lastCommand=-1;
  for(const auto& packet:sent) {
    assert(packet.second.size()<=1024);
    if(packet.first==0) {lastCommand=packet.second[0]; if(lastCommand==0x20)refresh=true;}
    else if(lastCommand==0x24 || lastCommand==0x26) planeBytes+=packet.second.size();
  }
  assert(planeBytes==2*kFrameBytes && refresh);
  // Reconstruct wire payloads; both planes must match the Seeed X-reflected UI.
  std::vector<uint8_t> expected(kFrameBytes), plane24, plane26;
  Frame expectedFrame(expected.data(), expected.size());
  assert(render(expectedFrame, DrawState{0, false, false, 0, false}));
  expectedFrame.mirrorX();
  lastCommand=-1;
  for (const auto& packet : sent) {
    if (packet.first==0) lastCommand=packet.second[0];
    else if (lastCommand==0x24) plane24.insert(plane24.end(),packet.second.begin(),packet.second.end());
    else if (lastCommand==0x26) plane26.insert(plane26.end(),packet.second.begin(),packet.second.end());
  }
  assert(plane24==expected && plane26==expected);
  assert(!display.draw(9, false, false, 0));
  assert(display.available());
  busyEnabled=false;
  auto start=fakeTime;
  assert(!display.draw(1, false, false, 0));
  assert(!display.available());
  assert(fakeTime-start<=10000);
  busyEnabled=true;
  assert(display.begin());
  stuckBusy=true;start=fakeTime;
  assert(!display.draw(2, false, false, 0));
  assert(fakeTime-start<=10000);
  assert(!display.available());
  stuckBusy=false;
  assert(display.begin());
  transferError=ESP_FAIL;
  assert(!display.draw(3, false, false, 0));
  assert(!display.available());
  transferError=0;
  assert(display.begin());
  completionTimeout=true;
  assert(!display.draw(3, false, false, 0));
  const auto countAfterTimeout=sent.size();
  assert(!display.begin()); // Must not reuse in-flight DMA buffer/transaction.
  assert(sent.size()==countAfterTimeout);
  completionTimeout=false;
  assert(display.begin()); // Drain the late completion before reinitializing.
  assert(display.draw(3, false, false, 0));
  // millis rollover cannot turn a bounded BUSY wait into an endless wait.
  fakeTime=UINT32_MAX-100;
  stuckBusy=true;start=fakeTime;
  assert(!display.draw(0, false, false, 0));
  assert(uint32_t(fakeTime-start)<=10000);
  puts("PASS SSD1677 transaction/cache/failure behavior with fake I/O (NOT hardware)");
#endif
}
