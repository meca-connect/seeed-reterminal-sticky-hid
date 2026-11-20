#pragma once
#include <cstdint>
#include <cmath>
namespace stickyapp {
struct Contact { uint8_t id; float x, y; };
enum ActionType { Move, Scroll, Click, Press, Release, Zoom, SwipeLeft, SwipeRight, SwipeUp, SwipeDown };
// Move: a=dx,b=dy. Scroll: a=horizontal,b=vertical ticks (positive follows touch).
// Click/Press/Release: a=button mask (1/2/4), b=0. Zoom: a=signed wheel ticks;
// the app owns modifier/report dispatch. Swipes carry a=b=0.
struct Action { ActionType type; int a, b; };
struct Actions {
    Action items[16]{};
    uint8_t count = 0;
    // Capacity is fixed; excess actions are dropped, never written past items.
    void add(ActionType t, int a = 0, int b = 0) {
        if (count < 16) items[count++] = {t, a, b};
    }
};

// v0.2 runtime feel parameters. DEFAULTS REPRODUCE v0.1 BEHAVIOR EXACTLY
// (smooth off, linear accel, 24px/tick, hard lock 1.7); firmware may load
// different values via setTuning() and iterate live without a reflash.
struct Tuning {
    float pointerGain        = 1.25f;
    float pointerAccel       = 0.018f;
    float pointerAccelExp    = 1.0f;   // v0.1 = linear; >1 = progressive curve
    float pointerAccelMax    = 2.6f;
    float pointerDeadzone    = 0.3f;
    float pointerSmoothAlpha = 1.0f;   // EMA alpha on input deltas; 1.0 = off
    float scrollPxPerTick    = 24.0f;
    float axisLockRatio      = 1.7f;
    float zoomPxPerTick      = 55.0f;
    float momentumTauMs      = 140.0f; // exponential decay time constant
    float momentumMinSpeed   = 0.05f;  // px/ms below which inertia stops
    float momentumMaxTicks   = 12.0f;  // per-tick-emission clamp (safety)
    // v0.3 edge strips. Defaults keep v0.2 behavior: 20px side strips
    // scroll vertically (content follows finger), bottom strip horizontally.
    float edgeWidthPx         = 20.0f;
    bool  leftStrip           = true;
    bool  rightStrip          = true;
    bool  sideStripHorizontal = false; // true: side-strip drag => horizontal scroll
    float sideStripSign       = 1.0f;  // -1 = scrollbar semantics (drag down => scroll down)
    float stripPxPerTick      = 24.0f;
    bool  bottomStrip         = true;
};

class GestureEngine {
public:
    // Portable bounded v0.2, reasoned from PaperS3 v1.0 state/output/loop.
    // All distances are logical pixels, times unsigned milliseconds; pointer
    // acceleration is per fresh frame, matching the source (not velocity-based).
    // Transport-, hardware- and screen-agnostic; caller owns dispatch.
    static constexpr uint32_t TAP_MAX_MS = 220;
    static constexpr float TAP_MAX_DIST_PX = 14.0f;
    static constexpr float POINTER_GAIN = 1.25f;
    static constexpr float POINTER_ACCEL = 0.018f;
    static constexpr float POINTER_ACCEL_MAX = 2.6f;
    static constexpr float POINTER_DEADZONE_PX = 0.3f;
    static constexpr uint32_t LONG_PRESS_MS = 350;
    static constexpr uint32_t DOUBLE_TAP_GAP_MS = 340;
    static constexpr float DOUBLE_TAP_RADIUS_PX = 45.0f;
    static constexpr float SANE_DELTA_PX = 220.0f; // Reject per-ID axis jumps, rebase.
    static constexpr int LOGICAL_WIDTH = 480, LOGICAL_HEIGHT = 800; // Sticky portrait default
    static constexpr int OUTPUT_LIMIT = 127;
    static constexpr float SCROLL_START_PX = 4.0f;
    static constexpr float SCROLL_PX_PER_TICK = 24.0f; // v0.1 default; Tuning overrides.
    static constexpr float ZOOM_START_PX = 26.0f, ZOOM_DOMINANCE = 1.25f;
    static constexpr float ZOOM_PX_PER_TICK = 55.0f; // Positive spread => positive Zoom.a.
    static constexpr float AXIS_LOCK_RATIO = 1.7f;
    static constexpr int EDGE_WIDTH_PX = 20; // Side/bottom strips, latched at touchdown.
    static constexpr float SWIPE_START_PX = 70.0f;
    static constexpr float SWIPE_AXIS_RATIO = 1.3f; // Diagonals wait for a dominant axis.
    static constexpr uint32_t DT_CLAMP_MS = 50;

    // v0.2: logical space is runtime-configurable (rotated UI may be 800x480).
    void setDimensions(int w, int h) {
        if (w > 0 && h > 0) { w_ = w; h_ = h; }
    }
    void setTuning(const Tuning& t) { t_ = t; }
    const Tuning& tuning() const { return t_; }

    // Call only for fresh frames, including an explicit zero-contact frame.
    // No timer-based release: a missing/no-new frame is not an update.
    // IDs must be unique, x in [0,w_), y in [0,h_). Invalid frames or >3
    // contacts release drag and suppress until all-up. Count is checked before
    // dereference. Caller must provide at least count contacts for counts 1..3.
    // Topology changes discard that frame's motion; after a non-drag lift,
    // wait for all-up (or a higher peak) rather than restart a trailing pointer.
    Actions update(const Contact* pts, uint8_t count, uint32_t now) {
        Actions out;
        uint32_t dt = lastNowMs_ ? clampDt(now - lastNowMs_) : 0;
        lastNowMs_ = now;
        if (!count) {
            if (drag_) out.add(Release, 1);
            if (active_ && tap_ && !blocked_ && now - startMs_ <= TAP_MAX_MS && travel_ <= TAP_MAX_DIST_PX) {
                out.add(Click, peak_ == 3 ? 4 : peak_ == 2 ? 2 : 1);
                if (peak_ == 1) {
                    lastTap_ = first_; lastTapMs_ = now; haveTap_ = true;
                }
            }
            // Start inertial scroll if the user released a live 2F scroll.
            if (active_ && !drag_ && !blocked_ && two_ == TwoMode::Scroll) startMomentum(now);
            drag_ = blocked_ = false;
            active_ = false;
            zone_ = ZoneNone;
            smX_ = smY_ = 0;
            return out;
        }
        if (blocked_) return out;
        if (!valid(pts, count)) return cancel();
        if (!active_) {
            active_ = tap_ = true;
            wait_ = false;
            momActive_ = false;                 // a new finger stops inertia
            peak_ = count;
            startMs_ = now;
            travel_ = resX_ = resY_ = 0;
            smX_ = smY_ = 0;
            first_ = pts[0];
            zone_ = ZoneNone;
            if (count == 1) {
                const float e = t_.edgeWidthPx;
                if ((t_.leftStrip && first_.x < e) || (t_.rightStrip && first_.x >= w_ - e)) zone_ = ZoneSideV;
                else if (t_.bottomStrip && first_.y >= h_ - e) zone_ = ZoneBottomH;
            }
            if (zone_ != ZoneNone) tap_ = false;
            resetMulti(pts, count);
            if (count == 1 && zone_ == ZoneNone && haveTap_ && now - lastTapMs_ <= DOUBLE_TAP_GAP_MS &&
                distance(first_, lastTap_) <= DOUBLE_TAP_RADIUS_PX) {
                drag_ = true; tap_ = false; out.add(Press, 1);
            }
            haveTap_ = false;
            save(pts, count, true);
            return out;
        }
        bool same = count == previousCount_;
        bool sane = true;
        float dx = 0, dy = 0;
        for (uint8_t i = 0; i < count; ++i) {
            const int old = find(pts[i].id);
            if (old < 0) { same = false; continue; }
            travel_ = std::fmax(travel_, distance(pts[i], origins_[old]));
            if (std::fabs(pts[i].x - previous_[old].x) > SANE_DELTA_PX ||
                std::fabs(pts[i].y - previous_[old].y) > SANE_DELTA_PX) sane = false;
            dx += pts[i].x - previous_[old].x;
            dy += pts[i].y - previous_[old].y;
        }
        if (count > peak_) { peak_ = count; wait_ = false; }
        if (count == 3 && drag_) {
            out.add(Release, 1); drag_ = false; // Takeover precedes every lower-priority output.
        }
        if (count < peak_ && !drag_) wait_ = true;
        if (!same) {
            // Added/removed IDs establish fresh baselines; never compare slots.
            // Pure addition or removal may still be a tap; replacement may not.
            uint8_t matched = 0;
            for (uint8_t i = 0; i < count; ++i) if (find(pts[i].id) >= 0) ++matched;
            const uint8_t smaller = count < previousCount_ ? count : previousCount_;
            if (matched < smaller) tap_ = false;
            save(pts, count, false);
            resetMulti(pts, count);
            if (count > 1) zone_ = ZoneNone;
            resX_ = resY_ = 0;
            if (!sane) tap_ = false;
            return out;
        }
        save(pts, count, false);
        if (!sane) {
            tap_ = false; resX_ = resY_ = 0; resetMulti(pts, count); return out;
        }
        if (drag_) pointer(out, dx, dy);
        else if (!wait_ && peak_ == 1) {
            if (zone_ == ZoneSideV) {
                const float k = t_.sideStripSign * t_.scrollPxPerTick / t_.stripPxPerTick;
                if (t_.sideStripHorizontal) scroll(out, -dy * k, 0, false, true);
                else scroll(out, 0, dy * k, true);
            }
            else if (zone_ == ZoneBottomH) scroll(out, dx, 0, false, true);
            else pointer(out, dx, dy);
        } else if (!wait_ && peak_ == 2) twoFinger(out, pts, dx / count, dy / count, dt);
        else if (!wait_ && peak_ == 3) swipe(out, dx / count, dy / count);
        if (!drag_ && !wait_ && peak_ == 1 && zone_ == ZoneNone && tap_ && now - startMs_ >= LONG_PRESS_MS &&
            travel_ <= TAP_MAX_DIST_PX) {
            drag_ = true; tap_ = false; out.add(Press, 1);
        }
        return out;
    }

    // v0.2: call every main-loop iteration (even without fresh frames) to emit
    // inertial scroll after a released two-finger swipe. Bounded: stops itself.
    Actions tick(uint32_t now) {
        Actions out;
        if (!momActive_ || blocked_) return out;
        uint32_t dt = clampDt(now - lastTickMs_);
        lastTickMs_ = now;
        const float decay = std::exp(-float(dt) / t_.momentumTauMs);
        momX_ *= decay; momY_ *= decay;
        if (t_.momentumTauMs > 0) {
            momResX_ += momX_ * float(dt) / t_.scrollPxPerTick;
            momResY_ += momY_ * float(dt) / t_.scrollPxPerTick;
        }
        int x = consume(momResX_), y = consume(momResY_);
        const float cap = t_.momentumMaxTicks;
        if (x > cap) { x = int(cap); }
        if (x < -cap) { x = -int(cap); }
        if (y > cap) { y = int(cap); }
        if (y < -cap) { y = -int(cap); }
        if (x || y) out.add(Scroll, x, y);
        if (std::hypot(momX_, momY_) < t_.momentumMinSpeed) { momActive_ = false; momResX_ = momResY_ = 0; }
        return out;
    }
    bool momentumActive() const { return momActive_; }

    // Screen changes/disconnect: release once, suppress until explicit all-up.
    Actions cancel() {
        Actions out;
        if (drag_) out.add(Release, 1);
        active_ = drag_ = haveTap_ = tap_ = false;
        blocked_ = true;
        momActive_ = false;
        return out;
    }
private:
    enum class Zone { None, SideV, BottomH };
    static constexpr auto ZoneNone = Zone::None;
    static constexpr auto ZoneSideV = Zone::SideV;
    static constexpr auto ZoneBottomH = Zone::BottomH;
    bool active_ = false, tap_ = false, wait_ = false;
    bool drag_ = false, blocked_ = false, haveTap_ = false;
    Zone zone_ = Zone::None;
    enum class TwoMode { Pending, Scroll, Zoom };
    TwoMode two_ = TwoMode::Pending;
    int axis_ = 0; // 0 undecided, 1 horizontal, 2 vertical.
    float scrollX_ = 0, scrollY_ = 0, zoom_ = 0;
    float commonX_ = 0, commonY_ = 0, baseSpread_ = 0, prevSpread_ = 0;
    uint8_t peak_ = 0, previousCount_ = 0;
    uint32_t startMs_ = 0;
    uint32_t lastTapMs_ = 0;
    Contact first_{}, lastTap_{};
    Contact previous_[3]{}, origins_[3]{};
    float travel_ = 0, resX_ = 0, resY_ = 0;
    int w_ = LOGICAL_WIDTH, h_ = LOGICAL_HEIGHT;
    Tuning t_{};
    // pointer input smoothing
    float smX_ = 0, smY_ = 0;
    // momentum state
    bool momActive_ = false;
    float velX_ = 0, velY_ = 0;          // EMA px/ms while 2F-scrolling
    float momX_ = 0, momY_ = 0, momResX_ = 0, momResY_ = 0;
    uint32_t lastNowMs_ = 0, lastTickMs_ = 0;

    static uint32_t clampDt(uint32_t dt) { return dt > DT_CLAMP_MS ? DT_CLAMP_MS : (dt ? dt : 1); }
    bool valid(const Contact* pts, uint8_t count) const {
        // Check count before dereferencing: a rejected oversized buffer need not exist.
        if (!pts || count > 3) return false;
        for (uint8_t i = 0; i < count; ++i) {
            if (!std::isfinite(pts[i].x) || !std::isfinite(pts[i].y) ||
                pts[i].x < 0 || pts[i].x >= w_ ||
                pts[i].y < 0 || pts[i].y >= h_) return false;
            for (uint8_t j = 0; j < i; ++j) if (pts[i].id == pts[j].id) return false;
        }
        return true;
    }
    int find(uint8_t id) const {
        for (uint8_t i = 0; i < previousCount_; ++i)
            if (previous_[i].id == id) return i;
        return -1;
    }
    void save(const Contact* pts, uint8_t count, bool reset) {
        Contact nextOrigins[3]{};
        for (uint8_t i = 0; i < count; ++i) {
            const int old = reset ? -1 : find(pts[i].id);
            nextOrigins[i] = old < 0 ? pts[i] : origins_[old];
        }
        for (uint8_t i = 0; i < count; ++i) {
            previous_[i] = pts[i]; origins_[i] = nextOrigins[i];
        }
        previousCount_ = count;
    }
    static float distance(const Contact& a, const Contact& b) {
        return std::hypot(a.x - b.x, a.y - b.y);
    }
    void resetMulti(const Contact* pts, uint8_t count) {
        two_ = TwoMode::Pending; axis_ = 0;
        scrollX_ = scrollY_ = zoom_ = commonX_ = commonY_ = 0;
        velX_ = velY_ = 0;
        baseSpread_ = prevSpread_ = count == 2 ? distance(pts[0], pts[1]) : 0;
    }
    void startMomentum(uint32_t now) {
        momX_ = axis_ == 2 ? 0 : velX_;
        momY_ = axis_ == 1 ? 0 : velY_;
        if (std::hypot(momX_, momY_) >= t_.momentumMinSpeed) {
            momActive_ = true;
            momResX_ = momResY_ = 0;
            lastTickMs_ = now;
        }
    }
    void swipe(Actions& out, float dx, float dy) {
        commonX_ += dx; commonY_ += dy;
        const float ax = std::fabs(commonX_), ay = std::fabs(commonY_);
        if (ax >= SWIPE_START_PX && ax > ay * SWIPE_AXIS_RATIO)
            out.add(commonX_ < 0 ? SwipeLeft : SwipeRight);
        else if (ay >= SWIPE_START_PX && ay > ax * SWIPE_AXIS_RATIO)
            out.add(commonY_ < 0 ? SwipeUp : SwipeDown);
        else return;
        // One directional action for the entire down/up session, not each frame.
        tap_ = false; blocked_ = true;
    }
    void scroll(Actions& out, float dx, float dy, bool forceVertical = false, bool forceHorizontal = false) {
        if (forceVertical) axis_ = 2;
        if (forceHorizontal) axis_ = 1;
        if (!axis_) {
            if (std::fabs(dx) > std::fabs(dy) * t_.axisLockRatio) axis_ = 1;
            else if (std::fabs(dy) > std::fabs(dx) * t_.axisLockRatio) axis_ = 2;
        }
        if (axis_ != 2) scrollX_ += dx / t_.scrollPxPerTick;
        if (axis_ != 1) scrollY_ += dy / t_.scrollPxPerTick;
        const int x = consume(scrollX_), y = consume(scrollY_);
        if (x || y) out.add(Scroll, x, y);
    }
    void twoFinger(Actions& out, const Contact* pts, float dx, float dy, uint32_t dt) {
        commonX_ += dx; commonY_ += dy;
        const float spread = distance(pts[0], pts[1]);
        bool classified = false;
        if (two_ == TwoMode::Pending) {
            const float travel = std::hypot(commonX_, commonY_);
            const float stretch = std::fabs(spread - baseSpread_);
            if (stretch > ZOOM_START_PX && stretch > travel * ZOOM_DOMINANCE) {
                two_ = TwoMode::Zoom; classified = true;
            } else if (travel > SCROLL_START_PX) {
                two_ = TwoMode::Scroll; classified = true;
            }
            if (classified) tap_ = false;
        }
        if (two_ == TwoMode::Scroll) {
            if (dt) { // velocity EMA in px/ms for release inertia
                velX_ = 0.7f * velX_ + 0.3f * (dx / float(dt));
                velY_ = 0.7f * velY_ + 0.3f * (dy / float(dt));
            }
            scroll(out, classified ? commonX_ : dx, classified ? commonY_ : dy);
        } else if (two_ == TwoMode::Zoom) {
            zoom_ += (spread - (classified ? baseSpread_ : prevSpread_)) / t_.zoomPxPerTick;
            const int ticks = consume(zoom_);
            if (ticks) out.add(Zoom, ticks);
        }
        prevSpread_ = spread;
    }
    static int consume(float& residual) {
        const int whole = static_cast<int>(residual);
        residual -= whole; // Discard saturated excess; retain only subpixel part.
        return whole > OUTPUT_LIMIT ? OUTPUT_LIMIT : whole < -OUTPUT_LIMIT ? -OUTPUT_LIMIT : whole;
    }
    void pointer(Actions& out, float dx, float dy) {
        const float a = t_.pointerSmoothAlpha;
        if (a < 1.0f) { smX_ = a * dx + (1.0f - a) * smX_; smY_ = a * dy + (1.0f - a) * smY_; }
        else { smX_ = dx; smY_ = dy; }
        const float speed = std::hypot(smX_, smY_);
        if (speed < t_.pointerDeadzone) return;
        const float accel = t_.pointerAccelExp == 1.0f ? speed : std::pow(speed, t_.pointerAccelExp);
        const float gain = std::fmin(t_.pointerAccelMax, t_.pointerGain + t_.pointerAccel * accel);
        resX_ += smX_ * gain; resY_ += smY_ * gain;
        const int x = consume(resX_), y = consume(resY_);
        if (x || y) out.add(Move, x, y);
    }
};
} // namespace stickyapp
