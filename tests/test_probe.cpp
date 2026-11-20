// Hardware APIs are faked only to exercise the actual sketch's failure path.
// These tests are not evidence of physical I2C/USB operation.
#include <cassert>
#include <cstdio>
#include "../touch_probe/touch_probe.ino"
int main() {
  Wire.availableDevice=false;
  setup();
  assert(gtAddress==0 && "failed detection must leave polling disabled");
  puts("PASS failed detection disables polling");
  Wire.availableDevice=true;
  assert(!probeAddress(0x5D) && "I2C ACK without GT911 product ID is not detection");
  Wire.regs[0x8140]='9'; Wire.regs[0x8141]='1'; Wire.regs[0x8142]='1';
  assert(probeAddress(0x5D));
  puts("PASS controller identity validation");
  Serial.output.clear();
  Wire.regs[0x8146]=0xE0; Wire.regs[0x8147]=0x01;
  Wire.regs[0x8148]=0x20; Wire.regs[0x8149]=0x03;
  setup();
  assert(Serial.output.find("sensorX=480 sensorY=800")!=std::string::npos);
  assert(pinLevels[45]==HIGH && pinLevels[46]==HIGH);
  puts("PASS runtime resolution and board power hold");
  Serial.output.clear();
  const uint8_t raw[] = {3, 0xDF, 0x01, 0x1F, 0x03, 0x34, 0x12, 0};
  for (unsigned i=0;i<8;++i) Wire.regs[0x814F+i]=raw[i];
  Wire.regs[0x814E]=0x81;
  fakeMs+=20; pollTouch();
  assert(Serial.output.find("TOUCH DOWN")!=std::string::npos);
  assert(Serial.output.find("id=3 x=479 y=799 size=4660")!=std::string::npos);
  assert(Wire.regs[0x814E]==0);
  Wire.regs[0x814E]=0x80;
  fakeMs+=20; pollTouch();
  assert(Serial.output.find("TOUCH UP")!=std::string::npos);
  fakeMs+=2000; loop();
  assert(Serial.output.find("ALIVE")!=std::string::npos);
  puts("PASS touch down/up, coordinates, acknowledgement and heartbeat");
}
