// Fake I2C/register fixtures: no physical hardware inference.
#include <cassert>
#include <cstdio>
#include <Arduino.h>
#include <Wire.h>
#include "../sticky_trackpad/StickyTouch.h"
int main() {
  StickyTouch touch;
  Wire.availableDevice=false;
  assert(!touch.begin());
  Wire.availableDevice=true;
  Wire.regs[0x8140]='9'; Wire.regs[0x8141]='1'; Wire.regs[0x8142]='1';
  Wire.regs[0x8146]=0xE0; Wire.regs[0x8147]=1;
  Wire.regs[0x8148]=0x20; Wire.regs[0x8149]=3;
  assert(touch.begin());
  assert(touch.sensorWidth()==480 && touch.sensorHeight()==800);
  StickyTouch::Frame f{};
  Wire.regs[0x814E]=0x83;
  for(unsigned i=0;i<3;++i) {
    uint16_t base=0x814F+i*8;
    Wire.regs[base]=i;
    Wire.regs[base+1]=10+i*40;
    Wire.regs[base+2]=0;
    Wire.regs[base+3]=20+i*50;
    Wire.regs[base+4]=0;
  }
  assert(touch.read(f)==StickyTouch::NewFrame && f.count==3);
  assert(f.points[2].id==2 && f.points[2].x==90 && f.points[2].y==120);
  assert(Wire.regs[0x814E]==0);
  // No fresh frame is NOT a release; old caller state must remain available.
  assert(touch.read(f)==StickyTouch::NoFrame && f.count==3);
  Wire.regs[0x814E]=0x80;
  assert(touch.read(f)==StickyTouch::NewFrame && f.count==0);
  Wire.availableDevice=false;
  assert(touch.read(f)==StickyTouch::Error);
  puts("PASS touch identity, dimensions, 3 contacts, ack, no-frame and release");
}
