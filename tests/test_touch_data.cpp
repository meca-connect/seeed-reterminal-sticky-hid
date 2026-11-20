// Synthetic register fixtures: host tests, not physical-device observations.
#include <cassert>
#include <cstdio>
#include "../touch_probe/touch_data.h"

int main() {
  const uint8_t id[] = {'9','1','1',0};
  const uint8_t wrong[] = {0,0,0,0};
  assert(sticky::isGT911(id));
  assert(!sticky::isGT911(wrong));
  puts("PASS product-ID validation");
  // GT911 records starting at 0x814F: ID, Xlo/Xhi, Ylo/Yhi, size lo/hi, reserved.
  const uint8_t raw[] = {3, 0xDF, 0x01, 0x1F, 0x03, 0x34, 0x12, 0};
  const auto p = sticky::decodePoint(raw);
  assert(p.id == 3 && p.x == 479 && p.y == 799 && p.size == 0x1234);
  puts("PASS little-endian contact decoding");
}
