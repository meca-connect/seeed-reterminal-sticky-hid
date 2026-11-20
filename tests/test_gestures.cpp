#include <cassert>
#include <cstdio>
#include <limits>
#include "../sticky_trackpad/GestureEngine.h"
using namespace stickyapp;

static bool has(const Actions& a, ActionType t, int value = 0) {
    for (uint8_t i = 0; i < a.count; ++i)
        if (a.items[i].type == t && (!value || a.items[i].a == value)) return true;
    return false;
}
static void pointer_and_tap() {
    GestureEngine g;
    Contact p{7, 200, 200};
    assert(g.update(&p, 1, 1000).count == 0);
    p.x += 10;
    const auto moved = g.update(&p, 1, 1010);
    assert(moved.count == 1 && moved.items[0].type == Move);
    assert(moved.items[0].a > 10 && moved.items[0].b == 0);
    assert(has(g.update(nullptr, 0, 1050), Click, 1));
    assert(g.update(nullptr, 0, 1060).count == 0);
    GestureEngine subpixel;
    p = {7, 200, 200};
    subpixel.update(&p, 1, 1000);
    int sum = 0;
    for (int i = 1; i <= 10; ++i) {
        p.x += 0.4f;
        auto a = subpixel.update(&p, 1, 1000 + i * 10);
        for (uint8_t j = 0; j < a.count; ++j) sum += a.items[j].a;
    }
    assert(sum >= 4 && sum <= 6);
    GestureEngine fast;
    p = {7, 200, 200}; fast.update(&p, 1, 1000);
    p.x += 100;
    auto a = fast.update(&p, 1, 1010);
    assert(a.count == 1 && a.items[0].a == 127);
    assert(fast.update(nullptr, 0, 1020).count == 0);
    Actions bounded;
    for (int i = 0; i < 100; ++i) bounded.add(Move, i, -i);
    assert(bounded.count == 16 && bounded.items[15].a == 15);
    puts("PASS pointer acceleration, subpixels, taps and bounded actions");
}
static void multi_contact_clicks_and_identity() {
    for (uint8_t count = 2; count <= 3; ++count) {
        GestureEngine g;
        Contact p[] = {{9, 100, 100}, {2, 400, 100}, {200, 450, 100}};
        assert(g.update(p, 1, 1000).count == 0);
        assert(g.update(p, count, 1020).count == 0);
        // Reordering is not motion, including arbitrary full-byte IDs.
        Contact reordered[] = {p[count - 1], p[0], p[1]};
        assert(g.update(reordered, count, 1040).count == 0);
        for (uint8_t n = count - 1; n > 0; --n)
            assert(g.update(reordered, n, 1060).count == 0);
        const auto a = g.update(nullptr, 0, 1100);
        assert(a.count == 1 && has(a, Click, count == 2 ? 2 : 4));
        assert(g.update(nullptr, 0, 1110).count == 0);
    }
    GestureEngine g;
    Contact p[] = {{3, 200, 100}, {8, 400, 100}};
    g.update(p, 2, 1000);
    g.update(p, 1, 1030);
    p[0].x += 30;
    assert(g.update(p, 1, 1040).count == 0);
    assert(g.update(nullptr, 0, 1100).count == 0);
    GestureEngine replaced;
    Contact one{3, 100, 100};
    replaced.update(&one, 1, 1000);
    one = {4, 420, 300};
    assert(replaced.update(&one, 1, 1010).count == 0);
    one.x += 5;
    assert(has(replaced.update(&one, 1, 1020), Move));
    assert(replaced.update(nullptr, 0, 1030).count == 0);
    puts("PASS peak-contact clicks, staged lift and per-ID topology changes");
}
static void dragging_and_cancel() {
    GestureEngine g;
    Contact p[] = {{7, 200, 200}, {9, 440, 200}, {2, 400, 200}};
    g.update(p, 1, 1000);
    auto a = g.update(p, 1, 1400);
    assert(a.count == 1 && has(a, Press, 1));
    assert(g.update(p, 1, 1410).count == 0);
    p[0].x += 10;
    assert(has(g.update(p, 1, 1420), Move));
    assert(g.update(p, 2, 1430).count == 0);
    Contact reordered[] = {p[1], p[0]};
    assert(g.update(reordered, 2, 1440).count == 0);
    reordered[0].x += 5;
    assert(has(g.update(reordered, 2, 1450), Move));
    assert(g.update(reordered, 1, 1460).count == 0);
    reordered[0].x += 5;
    assert(has(g.update(reordered, 1, 1470), Move));
    a = g.update(nullptr, 0, 1480);
    assert(a.count == 1 && has(a, Release, 1) && !has(a, Click));

    // A real first tap is required; boot-time timestamps must not arm drag.
    GestureEngine doubleTap;
    assert(doubleTap.update(p, 1, 10).count == 0);
    assert(has(doubleTap.update(nullptr, 0, 40), Click, 1));
    assert(has(doubleTap.update(p, 1, 100), Press, 1));
    a = doubleTap.cancel();
    assert(a.count == 1 && has(a, Release, 1));
    assert(doubleTap.cancel().count == 0);
    assert(doubleTap.update(p, 1, 150).count == 0);
    assert(doubleTap.update(nullptr, 0, 160).count == 0);
    assert(doubleTap.update(p, 1, 180).count == 0);
    assert(has(doubleTap.update(nullptr, 0, 200), Click, 1));
    GestureEngine far;
    far.update(p, 1, 1000); far.update(nullptr, 0, 1030);
    p[0].x += 200;
    assert(far.update(p, 1, 1100).count == 0);
    assert(far.update(p, 1, 1200).count == 0);
    puts("PASS long-press, double-tap drag, relay IDs and idempotent cancel");
}
static void invalid_frames_and_jumps() {
    for (int mode = 0; mode < 6; ++mode) {
        GestureEngine g;
        Contact p[] = {{1, 100, 100}, {2, 200, 100}, {3, 300, 100}, {4, 400, 100}};
        g.update(p, 1, 1000);
        g.update(p, 1, 1400); // held left button must always be released
        Actions a;
        if (mode == 0) a = g.update(p, 4, 1410);
        if (mode == 1) a = g.update(p, 255, 1410); // must not read p[4+]
        if (mode == 2) a = g.update(nullptr, 1, 1410);
        if (mode == 3) { p[0].x = std::numeric_limits<float>::quiet_NaN(); a = g.update(p, 1, 1410); }
        if (mode == 4) { p[0].y = 800; a = g.update(p, 1, 1410); }
        if (mode == 5) { p[1].id = p[0].id; a = g.update(p, 2, 1410); }
        assert(a.count == 1 && has(a, Release, 1));
        p[0] = {1, 100, 100};
        assert(g.update(p, 1, 1420).count == 0);
        assert(g.update(nullptr, 0, 1430).count == 0);
        assert(g.update(p, 1, 1500).count == 0);
        assert(has(g.update(nullptr, 0, 1550), Click, 1));
    }
    GestureEngine jump;
    Contact p{4, 100, 100};
    jump.update(&p, 1, 1000);
    p.y = 600;
    assert(jump.update(&p, 1, 1010).count == 0);
    p.x += 5;
    assert(has(jump.update(&p, 1, 1020), Move));
    assert(jump.update(nullptr, 0, 1030).count == 0);
    // Unsigned millis rollover is not a long hold or expired tap.
    GestureEngine rollover;
    p = {4, 100, 100};
    rollover.update(&p, 1, UINT32_MAX - 20);
    assert(has(rollover.update(nullptr, 0, 20), Click, 1));
    GestureEngine idle;
    assert(idle.update(&p, 1, 1000).count == 0);
    // No update is made for the long no-new-frame interval.
    assert(has(idle.update(nullptr, 0, 100000), Release) == false);
    assert(idle.update(nullptr, 0, 100010).count == 0);
    puts("PASS invalid-frame suppression, drag safety, jump rebase and clock wrap");
}
static void scrolling_and_zoom() {
    for (int axis = 0; axis < 2; ++axis) {
        GestureEngine g;
        Contact p[] = {{8, 200, 100}, {3, 400, 100}};
        g.update(p, 2, 1000);
        Contact swapped[] = {p[1], p[0]};
        assert(g.update(swapped, 2, 1010).count == 0);
        for (auto& c : swapped) { if (axis) c.x += 60; else c.y += 60; }
        auto a = g.update(swapped, 2, 1020);
        assert(a.count == 1 && a.items[0].type == Scroll);
        assert(axis ? a.items[0].a > 0 && a.items[0].b == 0 :
                      a.items[0].b > 0 && a.items[0].a == 0);
        assert(g.update(swapped, 1, 1030).count == 0);
        swapped[0].x += 50;
        assert(g.update(swapped, 1, 1040).count == 0);
        assert(g.update(nullptr, 0, 1050).count == 0);
    }
    GestureEngine zoom;
    Contact pair[] = {{1, 200, 200}, {2, 400, 200}};
    zoom.update(pair, 2, 1000);
    pair[0].x -= 40; pair[1].x += 40;
    auto a = zoom.update(pair, 2, 1010);
    assert(a.count == 1 && a.items[0].type == Zoom && a.items[0].a > 0);
    pair[0].x += 60; pair[1].x -= 60;
    a = zoom.update(pair, 2, 1020);
    assert(a.count == 1 && a.items[0].type == Zoom && a.items[0].a < 0);
    assert(zoom.update(nullptr, 0, 1030).count == 0);

    GestureEngine edge;
    Contact p{4, 470, 100};
    assert(edge.update(&p, 1, 1000).count == 0);
    p.y += 60;
    a = edge.update(&p, 1, 1010);
    assert(a.count == 1 && a.items[0].type == Scroll && a.items[0].b > 0);
    assert(edge.update(nullptr, 0, 1020).count == 0);
    assert(edge.update(&p, 1, 1100).count == 0);
    assert(edge.update(nullptr, 0, 1120).count == 0); // edge tap is never a click
    GestureEngine replacement;
    pair[0] = {1, 100, 100}; pair[1] = {2, 200, 100};
    replacement.update(pair, 2, 1000);
    pair[1] = {3, 440, 300};
    assert(replacement.update(pair, 2, 1010).count == 0);
    assert(replacement.update(pair, 2, 1020).count == 0);
    assert(replacement.update(nullptr, 0, 1030).count == 0);
    puts("PASS two-finger axis scroll, pinch, edge scroll and pair rebasing");
}
static void three_finger_swipes_and_takeover() {
    const ActionType directions[] = {SwipeLeft, SwipeRight, SwipeUp, SwipeDown};
    for (int direction = 0; direction < 4; ++direction) {
        GestureEngine g;
        Contact p[] = {{8, 100, 200}, {2, 200, 200}, {9, 300, 200}};
        g.update(p, 3, 1000);
        for (auto& c : p) {
            if (direction < 2) c.x += direction == 0 ? -80 : 80;
            else c.y += direction == 2 ? -80 : 80;
        }
        auto a = g.update(p, 3, 1010);
        assert(a.count == 1 && a.items[0].type == directions[direction]);
        for (int frame = 0; frame < 4; ++frame) {
            for (auto& c : p) c.x += 5;
            assert(g.update(p, 3, 1020 + frame * 10).count == 0);
        }
        assert(g.update(p, 2, 1070).count == 0);
        assert(g.update(p, 3, 1080).count == 0);
        assert(g.update(p, 1, 1090).count == 0);
        assert(g.update(nullptr, 0, 1100).count == 0);
        assert(g.update(p, 3, 1200).count == 0);
        for (auto& c : p) c.y += 80;
        a = g.update(p, 3, 1210);
        assert(a.count == 1 && a.items[0].type == SwipeDown);
    }
    for (int mode = 0; mode < 4; ++mode) {
        GestureEngine g;
        Contact p[] = {{4, 100, 100}, {5, 200, 100}, {1, 300, 100}};
        if (mode == 0) { g.update(p, 1, 1000); g.update(p, 1, 1400); }
        if (mode == 1) {
            g.update(p, 2, 1000); p[0].y += 60; p[1].y += 60;
            assert(has(g.update(p, 2, 1010), Scroll));
        }
        if (mode == 2) {
            g.update(p, 2, 1000); p[0].x -= 40; p[1].x += 40;
            assert(has(g.update(p, 2, 1010), Zoom));
        }
        if (mode == 3) { p[0].x = 470; g.update(p, 1, 1000); }
        auto a = g.update(p, 3, 1410);
        assert(mode == 0 ? a.count == 1 && has(a, Release, 1) : a.count == 0);
        for (auto& c : p) c.y += 80;
        a = g.update(p, 3, 1420);
        assert(a.count == 1 && a.items[0].type == SwipeDown);
        assert(g.update(nullptr, 0, 1430).count == 0);
    }
    GestureEngine diagonal;
    Contact p[] = {{1, 100, 100}, {2, 200, 100}, {3, 300, 100}};
    diagonal.update(p, 3, 1000);
    for (auto& c : p) { c.x += 80; c.y += 80; }
    assert(diagonal.update(p, 3, 1010).count == 0);
    for (auto& c : p) c.x += 60;
    auto a = diagonal.update(p, 3, 1020);
    assert(a.count == 1 && a.items[0].type == SwipeRight);
    puts("PASS directional one-shot swipes and exclusive third-finger takeover");
}
static void takeover_after_staged_lift() {
    GestureEngine g;
    Contact p[] = {{1, 200, 100}, {2, 300, 100}, {3, 400, 100}};
    g.update(p, 2, 1000);
    assert(g.update(p, 1, 1010).count == 0);
    // A higher peak is a new, higher-priority classifier, not a trailing finger.
    assert(g.update(p, 3, 1020).count == 0);
    for (auto& c : p) c.y += 80;
    auto a = g.update(p, 3, 1030);
    assert(a.count == 1 && a.items[0].type == SwipeDown);
    assert(g.update(nullptr, 0, 1040).count == 0);
    puts("PASS third-finger takeover after a staged two-finger lift");
}
static void replacement_cannot_be_a_tap() {
    GestureEngine g;
    Contact p[] = {{1, 200, 100}, {2, 400, 100}};
    g.update(p, 2, 1000);
    // Lift one contact while the remaining contact is replaced: not a tap.
    Contact replacement{9, 300, 300};
    assert(g.update(&replacement, 1, 1010).count == 0);
    assert(g.update(nullptr, 0, 1020).count == 0);
    puts("PASS replacement during staged lift cannot become a click");
}

static const Action* find(const Actions& a, ActionType t) {
    for (uint8_t i = 0; i < a.count; ++i) if (a.items[i].type == t) return &a.items[i];
    return nullptr;
}

void side_and_bottom_bars() {
    // Left strip: vertical drag -> wheel only; tap in strip never clicks.
    GestureEngine g;
    Contact p[1];
    p[0] = {1, 8.0f, 400.0f};
    assert(g.update(p, 1, 1000).count == 0);
    bool sawVertical = false;
    for (int i = 1; i <= 6; ++i) {
        p[0].y = 400.0f + i * 10.0f;
        if (has(g.update(p, 1, 1000 + i * 10), Scroll)) sawVertical = true;
    }
    assert(sawVertical);
    assert(g.update(nullptr, 0, 1080).count == 0); // no click from strip drag-release
    // Bottom strip: horizontal drag -> pan only, vertical locked out.
    GestureEngine b;
    Contact q[1];
    q[0] = {1, 240.0f, 790.0f};
    assert(b.update(q, 1, 2000).count == 0);
    bool panOnly = true, sawPan = false;
    for (int i = 1; i <= 10; ++i) {
        q[0].x = 240.0f + i * 8.0f;
        Actions r = b.update(q, 1, 2000 + i * 10);
        if (const Action* s = find(r, Scroll)) {
            sawPan = true;
            if (s->b != 0) panOnly = false; // vertical must stay 0 in bottom bar
            if (s->a <= 0) panOnly = false; // dragging right -> positive pan
        }
    }
    assert(sawPan && panOnly);
    assert(b.update(nullptr, 0, 2200).count == 0); // bar tap is never a click
    puts("PASS side strips scroll vertically, bottom bar pans horizontally only");
}

void momentum_inertia() {
    GestureEngine g;
    Tuning t; t.momentumTauMs = 100.0f; t.momentumMinSpeed = 0.05f;
    g.setTuning(t);
    Contact p[] = {{1, 200.0f, 400.0f}, {2, 260.0f, 400.0f}};
    uint32_t now = 5000;
    assert(g.update(p, 2, now).count == 0);
    // Steady downward two-finger motion at 0.5 px/ms for 100 ms.
    for (int i = 1; i <= 10; ++i) {
        p[0].y = 400.0f + i * 5.0f; p[1].y = 400.0f + i * 5.0f;
        g.update(p, 2, now + uint32_t(i) * 10);
    }
    g.update(nullptr, 0, now + 110);       // release frame may carry a final tick
    assert(g.momentumActive());
    // Inertia emits positive vertical ticks and decays to a stop.
    int emitted = 0, lastAbs = 999; bool decayed = false;
    for (int i = 1; i <= 60; ++i) {
        Actions tk = g.tick(now + 110 + uint32_t(i) * 10);
        if (const Action* s = find(tk, Scroll)) {
            assert(s->b >= 0);           // same direction as the swipe
            emitted += s->b;
            if (s->b < lastAbs) decayed = true;
            lastAbs = s->b;
        }
    }
    assert(emitted > 0 && decayed);
    assert(!g.momentumActive());         // bounded: stops itself
    // A new finger cancels inertia.
    Contact q[1];
    q[0] = {1, 200.0f, 300.0f};
    g.update(p, 2, now + 1000); g.update(nullptr, 0, now + 1010);
    g.update(q, 1, now + 1020);
    assert(!g.momentumActive());
    assert(g.tick(now + 1030).count == 0);
    g.update(nullptr, 0, now + 1040);
    puts("PASS two-finger release inertia emits decaying bounded ticks");
}

void runtime_dimensions() {
    GestureEngine g;
    g.setDimensions(800, 480); // rotated landscape UI space
    Contact p[1];
    p[0] = {1, 790.0f, 240.0f};
    assert(g.update(p, 1, 1000).count == 0);   // x=790 valid at width 800
    bool sawScroll = false;
    for (int i = 1; i <= 8; ++i) {
        p[0].y = 240.0f + i * 10.0f;
        if (has(g.update(p, 1, 1000 + uint32_t(i) * 10), Scroll)) sawScroll = true;
    }
    assert(sawScroll);                         // right strip at rotated width
    assert(g.update(nullptr, 0, 1100).count == 0);
    // Out-of-range for the new dims is rejected, not clamped.
    Contact far[1];
    far[0] = {1, 850.0f, 100.0f};
    g.update(far, 1, 1200);                    // invalid -> cancel/suppress path
    Contact near[1];
    near[0] = {1, 100.0f, 100.0f};
    assert(g.update(near, 1, 1210).count == 0); // suppressed until all-up
    assert(g.update(nullptr, 0, 1220).count == 0);
    puts("PASS runtime dimensions drive validation and strip zones");
}
int main() {
    pointer_and_tap();
    multi_contact_clicks_and_identity();
    dragging_and_cancel();
    invalid_frames_and_jumps();
    scrolling_and_zoom();
    three_finger_swipes_and_takeover();
    takeover_after_staged_lift();
    replacement_cannot_be_a_tap();
    side_and_bottom_bars();
    momentum_inertia();
    runtime_dimensions();
    puts("All gesture tests passed");
}
