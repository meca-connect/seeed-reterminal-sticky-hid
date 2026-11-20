#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <map>
struct FakeWire {
 bool availableDevice = false;
 std::map<uint16_t,uint8_t> regs;
 uint16_t reg = 0;
 std::vector<uint8_t> tx;
 size_t left=0;
 bool begin(int,int,int) { return true; }
 void setTimeOut(unsigned) {}
 void beginTransmission(uint8_t) { tx.clear(); }
 void write(uint8_t v) { tx.push_back(v); }
 int endTransmission(bool=true) {
  if (!availableDevice) return 2;
  if (tx.size()>=2) reg=(tx[0]<<8)|tx[1];
  if (tx.size()==3) regs[reg]=tx[2];
  return 0;
 }
 size_t requestFrom(int,int n) { left=availableDevice ? n : 0; return left; }
 int available() { return left; }
 int read() { --left; return regs[reg++]; }
};
inline FakeWire Wire;
