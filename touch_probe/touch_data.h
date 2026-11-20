#pragma once
#include <cstdint>

namespace sticky {
struct Point { uint8_t id; uint16_t x, y, size; };
inline Point decodePoint(const uint8_t *p) {
  return {p[0], static_cast<uint16_t>(p[1] | (p[2] << 8)),
          static_cast<uint16_t>(p[3] | (p[4] << 8)),
          static_cast<uint16_t>(p[5] | (p[6] << 8))};
}
inline bool isGT911(const uint8_t *id) {
  return id[0] == '9' && id[1] == '1' && id[2] == '1' && id[3] == 0;
}
}
