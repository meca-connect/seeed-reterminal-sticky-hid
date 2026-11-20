#include <cassert>
#include <cstdio>
#include "../sketches/s9_sticky_ptp_ble/gt911_input.h"
int main(){
 stickyptp::Touch t;Wire.availableDevice=true;
 Wire.regs[0x8140]='9';Wire.regs[0x8141]='1';Wire.regs[0x8142]='1';Wire.regs[0x8143]=0;
 assert(t.begin() && t.address==0x5d && fakeMs==420);
 stickyptp::Contact c[4]{};
 assert(t.poll(c)==-1);
 Wire.regs[0x814e]=0x81;Wire.regs[0x814f]=3;Wire.regs[0x8150]=10;
 assert(t.poll(c)==1 && c[0].id==3 && c[0].x==10 && c[0].y==799);
 assert(Wire.regs[0x814e]==0 && t.frames==1);
 Wire.regs[0x814e]=0x80;assert(t.poll(c)==0);
 Wire.regs[0x814e]=0x85;assert(t.poll(c)==-2 && t.invalid==1 && Wire.regs[0x814e]==0);
 Wire.availableDevice=false;assert(t.poll(c)==-2 && t.errors==1);
 Wire.availableDevice=true;Wire.regs[0x8140]='8';assert(!t.begin() && t.address==0);
 puts("PASS actual GT911 adapter: product911, init timing, ready/no-data/release, malformed ack, I2C failure");
}
