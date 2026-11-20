// Host tests for the MVP UI logic and the v0.3 edge-strip engine options.
// g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined tests/test_mvp_ui.cpp -o build/test_mvp_ui && build/test_mvp_ui
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../sticky_trackpad/MvpUi.h"
#include "../sticky_trackpad/GestureEngine.h"
using namespace stickymvp;
using namespace stickyapp;

static void rotation_roundtrip() {
    for (uint8_t r = 0; r < 4; ++r) {
        Rotation rot{r};
        assert(rot.width() * rot.height() == PW * PH);
        for (int px = 0; px < PW; px += 7)
            for (int py = 0; py < PH; py += 11) {
                int sx, sy, qx, qy;
                rot.toScreen(px, py, sx, sy);
                assert(sx >= 0 && sx < rot.width() && sy >= 0 && sy < rot.height());
                rot.toPortrait(sx, sy, qx, qy);
                assert(qx == px && qy == py);
            }
    }
    // r=1: screen top-left sits at portrait top-right (UI turned 90 CW).
    int px, py; Rotation{1}.toPortrait(0, 0, px, py);
    assert(px == PW - 1 && py == 0);
    Rotation{2}.toPortrait(0, 0, px, py);
    assert(px == PW - 1 && py == PH - 1);
    puts("PASS rotation round-trips for 0/90/180/270");
}

static void settings_layout() {
    for (uint8_t r = 0; r < 4; ++r) {
        Rotation rot{r};
        SettingsLayout L(rot.width(), rot.height());
        for (int i = 0; i < BtnCount; ++i) {
            const Rect& b = L.btn[i];
            assert(b.h >= 60 && b.x >= 0 && b.y >= SettingsLayout::HEADER);
            assert(b.x + b.w <= rot.width() && b.y + b.h <= rot.height());
            assert(L.hit(b.x + b.w / 2, b.y + b.h / 2) == Button(i));
            if (i) assert(b.y >= L.btn[i - 1].y + L.btn[i - 1].h); // no overlap
        }
        assert(L.hit(1, 1) == BtnNone);
    }
    puts("PASS settings buttons fit, don't overlap and hit-test in every rotation");
}

static void taps() {
    TapDetector t; int x = -1, y = -1;
    assert(!t.update(1, 100, 200, 1000, x, y));
    assert(!t.update(1, 110, 205, 1050, x, y));
    assert(t.update(0, 0, 0, 1150, x, y) && x == 100 && y == 200);
    assert(!t.update(0, 0, 0, 1160, x, y));              // no repeat
    t.update(1, 100, 200, 2000, x, y); t.update(1, 200, 200, 2010, x, y);
    assert(!t.update(0, 0, 0, 2050, x, y));              // dragged
    t.update(1, 100, 200, 3000, x, y);
    assert(!t.update(0, 0, 0, 3000 + TapDetector::MAX_MS + 1, x, y)); // too long
    t.update(1, 100, 200, 4000, x, y); t.update(2, 100, 200, 4010, x, y);
    assert(!t.update(0, 0, 0, 4050, x, y));              // multi-touch
    t.update(2, 100, 200, 5000, x, y);
    assert(!t.update(0, 0, 0, 5050, x, y));              // started multi
    puts("PASS tap detector accepts short still taps only");
}

static void forget_confirm() {
    ForgetConfirm f;
    assert(!f.press(1000) && f.armed());
    assert(!f.expired(5999));
    assert(f.press(5999) && !f.armed());
    assert(!f.press(10000));
    assert(f.expired(15001) && !f.armed() && !f.expired(15002));
    assert(!f.press(20000)); f.cancel(); assert(!f.armed());
    assert(!f.press(20100));                             // cancel resets to first step
    puts("PASS forget requires two taps inside the window");
}

static int sumPan(const Actions& a, int& wheel) {
    int pan = 0;
    for (uint8_t i = 0; i < a.count; ++i)
        if (a.items[i].type == Scroll) { pan += a.items[i].a; wheel += a.items[i].b; }
        else assert(a.items[i].type != Move && a.items[i].type != Click);
    return pan;
}

static void horizontal_side_strips() {
    Tuning t; t.edgeWidthPx = 48; t.sideStripHorizontal = true; t.bottomStrip = false;
    for (float x : {10.0f, 470.0f}) {                    // left and right strips
        GestureEngine g; g.setTuning(t);
        Contact p{1, x, 600};
        g.update(&p, 1, 1000);
        int pan = 0, wheel = 0;
        for (int i = 1; i <= 10; ++i) { p.y -= 12; pan += sumPan(g.update(&p, 1, 1000 + i * 10), wheel); }
        assert(pan > 0 && wheel == 0);                   // drag up => scroll right
        sumPan(g.update(nullptr, 0, 1200), wheel);
        g.update(&p, 1, 1300);
        for (int i = 1; i <= 10; ++i) { p.y += 12; pan += sumPan(g.update(&p, 1, 1300 + i * 10), wheel); }
        assert(pan == 0 && wheel == 0);                  // drag down cancels it
    }
    // Bottom strip disabled: a bottom-edge drag is plain pointer motion.
    GestureEngine g; g.setTuning(t);
    Contact p{1, 240, 790};
    g.update(&p, 1, 1000); p.x += 20;
    auto a = g.update(&p, 1, 1010);
    assert(a.count == 1 && a.items[0].type == Move);
    // Landscape dimensions: the strip follows the rotated right edge.
    GestureEngine land; land.setTuning(t); land.setDimensions(800, 480);
    Contact q{1, 790, 300};
    land.update(&q, 1, 1000); q.y -= 60;
    a = land.update(&q, 1, 1010);
    assert(a.count == 1 && a.items[0].type == Scroll && a.items[0].a > 0 && a.items[0].b == 0);
    // v0.3 scrollbar: vertical, drag down => scroll down (wheel negative),
    // finer ticks, right strip only.
    Tuning sb; sb.edgeWidthPx = 34; sb.leftStrip = false; sb.sideStripSign = -1; sb.stripPxPerTick = 12;
    sb.bottomStrip = false;
    GestureEngine bar; bar.setTuning(sb);
    Contact r{1, 470, 300};
    bar.update(&r, 1, 1000);
    int wheel = 0;
    for (int i = 1; i <= 10; ++i) { r.y += 12; int pan = sumPan(bar.update(&r, 1, 1000 + i * 10), wheel); assert(pan == 0); }
    assert(wheel == -10);                                // 120px / 12px per tick
    GestureEngine noLeft; noLeft.setTuning(sb);
    Contact lft{1, 10, 300};
    noLeft.update(&lft, 1, 1000); lft.y += 30;
    a = noLeft.update(&lft, 1, 1010);
    assert(a.count == 1 && a.items[0].type == Move);     // disabled strip = pointer
    puts("PASS strips: horizontal option, scrollbar polarity/ticks, per-side enable");
}

int main() {
    rotation_roundtrip();
    settings_layout();
    taps();
    forget_confirm();
    horizontal_side_strips();
    puts("ALL MVP UI TESTS PASSED");
}
