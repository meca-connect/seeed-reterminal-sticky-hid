#pragma once
// Asynchronous full/partial e-paper refresh for the Sticky SSD1677, layered on
// ../_common/sticky_hw.h (which owns SPI, the framebuffer sFb and the
// verified portrait->SSD transpose).
//
// Sequence follows Seeed's own seeed_epaper ssd1677 driver:
//   full    : HW reset, SW reset 0x12, border 0x01, update ctrl 0xF7
//   partial : HW reset (no 0x12), border 0x80, update ctrl 0xFF, with the
//             previous image in RAM 0x26 and the new image in RAM 0x24
// Between refreshes the controller is put in deep sleep (0x10=0x01) to save
// power; every refresh re-initialises it and rewrites both RAM planes, so no
// state depends on RAM retention. Touch keeps running while BUSY is high.
#include "../_common/sticky_hw.h"

static uint8_t* sPrev = nullptr;
static bool sEpdBusy = false, sEpdFull = false;
static uint32_t sEpdStart = 0, sEpdLastMs = 0;
static uint8_t sEpdPartials = 0;

static bool epdInit(bool full) {
  digitalWrite(17, LOW); delay(5); digitalWrite(17, HIGH); delay(5);
  if (!sWaitBusy(2000)) return false;
  if (full && !(sSpiCmd(0x12) && sWaitBusy(2000))) return false;
  const uint8_t softstart[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
  const uint8_t driverout[] = {0xDF, 0x01, 0x02};
  const uint8_t xwin[] = {0, 0, 0x1F, 0x03};
  const uint8_t ywin[] = {0, 0, 0xDF, 0x01};
  return sReg8(0x18, 0x80) && sReg8(0x3C, full ? 0x01 : 0x80) &&
         sSpiCmd(0x0C) && sSpiXfer(true, softstart, sizeof softstart) &&
         sSpiCmd(0x01) && sSpiXfer(true, driverout, sizeof driverout) && sReg8(0x11, 0x03) &&
         sSpiCmd(0x44) && sSpiXfer(true, xwin, sizeof xwin) &&
         sSpiCmd(0x45) && sSpiXfer(true, ywin, sizeof ywin) && sWaitBusy(2000);
}
static bool epdCursorHome() {
  const uint8_t zero[] = {0, 0};
  return sSpiCmd(0x4E) && sSpiXfer(true, zero, 2) && sSpiCmd(0x4F) && sSpiXfer(true, zero, 2);
}

static bool epdBegin() {
  if (!stickyDisplayBegin()) return false;
  sPrev = (uint8_t*)heap_caps_malloc(SSD_FRAME, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!sPrev) return false;
  memset(sPrev, 0xFF, SSD_FRAME);
  return true;
}
static bool epdBusy() { return sEpdBusy; }

// Kick off a refresh of the current framebuffer. Returns false if busy/failed.
static bool epdStart(bool full) {
  if (!sDisplayReady || sEpdBusy) return false;
  if (!full && sEpdPartials >= 10) full = true;   // periodic clean against ghosting
  if (!epdInit(full)) { Serial.println("[EPD] init failed"); return false; }
  bool ok = epdCursorHome() && sSpiCmd(0x26) && sSpiXfer(true, full ? sFb : sPrev, SSD_FRAME) &&
            epdCursorHome() && sSpiCmd(0x24) && sSpiXfer(true, sFb, SSD_FRAME) &&
            sReg8(0x22, full ? 0xF7 : 0xFF) && sSpiCmd(0x20);
  if (!ok) { Serial.println("[EPD] write failed"); return false; }
  memcpy(sPrev, sFb, SSD_FRAME);                 // the panel now holds this image
  sEpdPartials = full ? 0 : sEpdPartials + 1;
  sEpdBusy = true; sEpdFull = full; sEpdStart = millis();
  return true;
}

// Call every loop. Returns true once when a refresh completes.
static bool epdPoll() {
  if (!sEpdBusy) return false;
  if (digitalRead(18) == HIGH) {
    if (millis() - sEpdStart > 8000) { sEpdBusy = false; Serial.println("[EPD] busy timeout"); }
    return false;
  }
  sEpdBusy = false;
  sEpdLastMs = millis() - sEpdStart;
  sReg8(0x10, 0x01);                             // controller deep sleep until next refresh
  return true;
}
