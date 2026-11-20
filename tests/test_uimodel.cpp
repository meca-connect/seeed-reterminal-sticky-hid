#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../sticky_trackpad/UIModel.h"
using namespace stickyapp;
static void layout_geometry() {
    assert(keyCount(Trackpad) == 0 && keyCount(Settings) == 0);
    assert(keyCount(Numpad) == 12 && keyCount(Keyboard) == 50);
    assert(keyCount(static_cast<Screen>(99)) == 0);
    for (Screen screen : {Numpad, Keyboard}) {
        for (int i = 0; i < keyCount(screen); ++i) {
            const auto k = keyAt(screen, i);
            assert(k.label && k.label[0] && k.usage && k.w > 0 && k.h > 0);
            assert(k.x >= 0 && k.y >= 80 && k.x + k.w <= 800 && k.y + k.h <= 440);
            assert(hitKey(screen, k.x + k.w / 2, k.y + k.h / 2) == i);
            assert(hitKey(screen, k.x, k.y) == i);
            assert(hitKey(screen, k.x + k.w - 1, k.y + k.h - 1) == i);
            assert(hitKey(screen, k.x + k.w, k.y + k.h) != i);
            const auto shifted = keyAt(screen, i, true);
            assert(shifted.x == k.x && shifted.y == k.y && shifted.w == k.w && shifted.h == k.h);
            for (int j = 0; j < i; ++j) {
                const auto p = keyAt(screen, j);
                assert(k.x + k.w <= p.x || p.x + p.w <= k.x ||
                       k.y + k.h <= p.y || p.y + p.h <= k.y);
            }
        }
        assert(hitKey(screen, -1, 100) == -1);
        assert(hitKey(screen, 800, 100) == -1);
        assert(hitKey(screen, 10, 79) == -1);
        assert(hitKey(screen, 10, 440) == -1);
        assert(hitKey(screen, 0, 0) == -1);
        for (int invalid : {-1, keyCount(screen), 1000000}) {
            const auto k = keyAt(screen, invalid);
            assert(k.usage == 0 && k.w == 0 && k.h == 0);
        }
    }
    assert(hitKey(Trackpad, 100, 100) == -1);
    assert(hitKey(Settings, 100, 100) == -1);
    assert(keyAt(static_cast<Screen>(99), 0).usage == 0);
    puts("PASS contained, disjoint grids, hit boundaries and invalid indices");
}
static void key_usages_and_shift() {
    const char* numpadLabels[] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "Enter"};
    const uint8_t numpadUsages[] = {0x5f, 0x60, 0x61, 0x5c, 0x5d, 0x5e, 0x59, 0x5a, 0x5b, 0x63, 0x62, 0x58};
    for (int i = 0; i < 12; ++i) {
        const auto k = keyAt(Numpad, i, true);
        assert(std::strcmp(k.label, numpadLabels[i]) == 0);
        assert(k.usage == numpadUsages[i] && k.modifiers == 0);
    }
    bool letters[26]{}, digits[10]{};
    bool shift = false, space = false, backspace = false, enter = false;
    for (int i = 0; i < 50; ++i) {
        const auto k = keyAt(Keyboard, i);
        const auto s = keyAt(Keyboard, i, true);
        assert(k.modifiers == 0 && s.usage == k.usage);
        if (k.usage >= 0x04 && k.usage <= 0x1d) {
            const int letter = k.usage - 0x04;
            letters[letter] = true;
            assert(k.label[0] == 'a' + letter && k.label[1] == 0);
            assert(s.label[0] == 'A' + letter && s.label[1] == 0);
            assert(s.modifiers == 0x02);
        } else if (k.usage >= 0x1e && k.usage <= 0x27) {
            const int digit = k.usage == 0x27 ? 0 : k.usage - 0x1d;
            digits[digit] = true;
            assert(k.label[0] == '0' + digit);
            assert(s.modifiers == 0x02 && std::strcmp(s.label, k.label) != 0);
        } else if (k.usage == 0xff) { shift = true; assert(s.modifiers == 0); }
        else if (k.usage == 0x2c) { space = true; assert(s.modifiers == 0); }
        else if (k.usage == 0x2a) { backspace = true; assert(s.modifiers == 0); }
        else if (k.usage == 0x28) { enter = true; assert(s.modifiers == 0); }
        else assert(s.modifiers == 0 || s.modifiers == 0x02);
    }
    for (bool found : letters) assert(found);
    for (bool found : digits) assert(found);
    assert(shift && space && backspace && enter);
    puts("PASS USB key usages, all alphanumerics and shifted labels/modifiers");
}
int main() {
    layout_geometry();
    key_usages_and_shift();
    puts("All UI model tests passed");
}
