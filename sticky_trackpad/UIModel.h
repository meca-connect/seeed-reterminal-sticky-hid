#pragma once
#include <cstdint>
namespace stickyapp {
enum Screen { Trackpad = 0, Numpad = 1, Keyboard = 2, Settings = 3 };
struct Key { const char* label; uint8_t usage; uint8_t modifiers; int x, y, w, h; };
// Physical button handling, screen switching and shift state belong to the app.
inline constexpr int LOGICAL_WIDTH = 800, LOGICAL_HEIGHT = 480;
inline constexpr int HEADER_HEIGHT = 80, FOOTER_HEIGHT = 40;
inline constexpr int KEY_GAP = 8, GRID_MARGIN = 8;
inline int keyCount(Screen screen) {
    return screen == Numpad ? 12 : screen == Keyboard ? 50 : 0;
}
// Rectangles are half-open: [x,x+w), [y,y+h). Invalid keys have usage/w/h=0.
inline Key keyAt(Screen screen, int index, bool shifted = false) {
    if (index < 0 || index >= keyCount(screen)) return {"", 0, 0, 0, 0, 0, 0};
    const int columns = screen == Numpad ? 3 : 10;
    const int rows = screen == Numpad ? 4 : 5;
    const int cellW = (LOGICAL_WIDTH - 2 * GRID_MARGIN) / columns;
    const int cellH = (LOGICAL_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT - 2 * GRID_MARGIN) / rows;
    struct Spec { const char* label; const char* upper; uint8_t usage; };
    static constexpr Spec numpad[] = {
        {"7","7",0x5f}, {"8","8",0x60}, {"9","9",0x61},
        {"4","4",0x5c}, {"5","5",0x5d}, {"6","6",0x5e},
        {"1","1",0x59}, {"2","2",0x5a}, {"3","3",0x5b},
        {".",".",0x63}, {"0","0",0x62}, {"Enter","Enter",0x58}
    };
    // US keyboard HID usages; equal-size cells keep drawing/hit-testing identical.
    static constexpr Spec keyboard[] = {
        {"1","!",0x1e}, {"2","@",0x1f}, {"3","#",0x20}, {"4","$",0x21}, {"5","%",0x22},
        {"6","^",0x23}, {"7","&",0x24}, {"8","*",0x25}, {"9","(",0x26}, {"0",")",0x27},
        {"q","Q",0x14}, {"w","W",0x1a}, {"e","E",0x08}, {"r","R",0x15}, {"t","T",0x17},
        {"y","Y",0x1c}, {"u","U",0x18}, {"i","I",0x0c}, {"o","O",0x12}, {"p","P",0x13},
        {"a","A",0x04}, {"s","S",0x16}, {"d","D",0x07}, {"f","F",0x09}, {"g","G",0x0a},
        {"h","H",0x0b}, {"j","J",0x0d}, {"k","K",0x0e}, {"l","L",0x0f}, {"Backspace","Backspace",0x2a},
        {"Shift","Shift",0xff}, {"z","Z",0x1d}, {"x","X",0x1b}, {"c","C",0x06}, {"v","V",0x19},
        {"b","B",0x05}, {"n","N",0x11}, {"m","M",0x10}, {",","<",0x36}, {".",">",0x37},
        {"Esc","Esc",0x29}, {"Tab","Tab",0x2b}, {"-","_",0x2d}, {"=","+",0x2e}, {"Space","Space",0x2c},
        {"/","?",0x38}, {";",":",0x33}, {"'","\"",0x34}, {"Delete","Delete",0x4c}, {"Enter","Enter",0x28}
    };
    const auto& spec = screen == Numpad ? numpad[index] : keyboard[index];
    const bool useShift = shifted && screen == Keyboard && spec.label[0] != spec.upper[0];
    return {useShift ? spec.upper : spec.label, spec.usage, static_cast<uint8_t>(useShift ? 0x02 : 0),
            GRID_MARGIN + (index % columns) * cellW,
            HEADER_HEIGHT + GRID_MARGIN + (index / columns) * cellH,
            cellW - KEY_GAP, cellH - KEY_GAP};
}
inline int hitKey(Screen screen, int x, int y) {
    for (int i = 0; i < keyCount(screen); ++i) {
        const auto k = keyAt(screen, i);
        if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) return i;
    }
    return -1;
}
} // namespace stickyapp
