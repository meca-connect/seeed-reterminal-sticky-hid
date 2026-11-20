#pragma once
// MVP UI logic (docs/DESIGN.md R5-R8): rotation, settings layout, tap
// detection and the two-step FORGET confirm. Pure C++, no Arduino, so it is
// host-tested (tests/test_mvp_ui.cpp). The sketch owns drawing and BLE.
#include <cstdint>

namespace stickymvp {

enum Screen : uint8_t { SCR_TRACKPAD, SCR_SETTINGS };

static constexpr int PW = 480, PH = 800;   // physical portrait logical frame

// Rotation r = 0..3 (x90 degrees clockwise of the UI relative to portrait,
// USB-C at the bottom for r=0). Screen space is what the user sees upright.
struct Rotation {
    uint8_t r = 0;
    int width()  const { return (r & 1) ? PH : PW; }
    int height() const { return (r & 1) ? PW : PH; }
    // Screen (sx,sy) -> portrait (px,py), used for drawing.
    void toPortrait(int sx, int sy, int& px, int& py) const {
        switch (r & 3) {
            case 0: px = sx;          py = sy;          break;
            case 1: px = PW - 1 - sy; py = sx;          break;
            case 2: px = PW - 1 - sx; py = PH - 1 - sy; break;
            default: px = sy;         py = PH - 1 - sx; break;
        }
    }
    // Portrait (px,py) -> screen (sx,sy), used for touch.
    void toScreen(int px, int py, int& sx, int& sy) const {
        switch (r & 3) {
            case 0: sx = px;          sy = py;          break;
            case 1: sx = py;          sy = PW - 1 - px; break;
            case 2: sx = PW - 1 - px; sy = PH - 1 - py; break;
            default: sx = PH - 1 - py; sy = px;         break;
        }
    }
};

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

enum Button : int8_t { BtnNone = -1, BtnRotate = 0, BtnForget = 1, BtnBack = 2, BtnCount = 3 };

// Settings layout in screen space: header + status, then three large stacked
// buttons centred in the remaining area. Works for portrait and landscape.
struct SettingsLayout {
    static constexpr int HEADER = 150, MARGIN = 24, GAP = 20, MAX_BTN_H = 140;
    Rect btn[BtnCount];
    explicit SettingsLayout(int w, int h) {
        int avail = h - HEADER - MARGIN;
        int bh = (avail - GAP * (BtnCount - 1)) / BtnCount;
        if (bh > MAX_BTN_H) bh = MAX_BTN_H;
        int total = bh * BtnCount + GAP * (BtnCount - 1);
        int y = HEADER + (avail - total) / 2;
        for (int i = 0; i < BtnCount; ++i) btn[i] = {MARGIN, y + i * (bh + GAP), w - 2 * MARGIN, bh};
    }
    Button hit(int x, int y) const {
        for (int i = 0; i < BtnCount; ++i) if (btn[i].contains(x, y)) return Button(i);
        return BtnNone;
    }
};

// Single-finger tap: down then all-up within maxMs and maxTravel of the
// touchdown point. Any multi-touch frame voids the tap. Returns true once,
// on release, with the touchdown position.
class TapDetector {
public:
    static constexpr uint32_t MAX_MS = 600;
    static constexpr int MAX_TRAVEL = 30;
    // count = contacts in this fresh frame; (x,y) = first contact (screen).
    bool update(uint8_t count, int x, int y, uint32_t now, int& tx, int& ty) {
        if (count == 0) {
            bool ok = down_ && valid_ && now - t0_ <= MAX_MS;
            down_ = false;
            if (ok) { tx = x0_; ty = y0_; }
            return ok;
        }
        if (!down_) { down_ = true; valid_ = (count == 1); x0_ = x; y0_ = y; t0_ = now; return false; }
        if (count != 1) valid_ = false;
        int dx = x - x0_, dy = y - y0_;
        if (dx * dx + dy * dy > MAX_TRAVEL * MAX_TRAVEL) valid_ = false;
        return false;
    }
    void reset() { down_ = false; }
private:
    bool down_ = false, valid_ = false;
    int x0_ = 0, y0_ = 0;
    uint32_t t0_ = 0;
};

// FORGET needs two taps within WINDOW_MS. press() returns true when the
// second (confirming) tap lands; expired() reports a lapsed arm once so the
// UI can redraw the button back to its idle label.
class ForgetConfirm {
public:
    static constexpr uint32_t WINDOW_MS = 5000;
    bool armed() const { return armed_; }
    bool press(uint32_t now) {
        if (armed_ && now - t_ <= WINDOW_MS) { armed_ = false; return true; }
        armed_ = true; t_ = now; return false;
    }
    bool expired(uint32_t now) {
        if (armed_ && now - t_ > WINDOW_MS) { armed_ = false; return true; }
        return false;
    }
    void cancel() { armed_ = false; }
private:
    bool armed_ = false;
    uint32_t t_ = 0;
};

} // namespace stickymvp
