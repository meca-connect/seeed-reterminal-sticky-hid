#pragma once
// Sticky-specific GT911 access, preserving hardware-tested touch_probe setup.
// Provenance: Seeed Sticky_peripheral_demo, plus project raw-register probe.
// No GT911 configuration writes; only data-ready acknowledgement.
#include <Arduino.h>
#include <Wire.h>
#include <cstdint>

class StickyTouch {
public:
  struct Point { uint8_t id; uint16_t x,y,size; };
  struct Frame { uint8_t count=0; Point points[5]{}; };
  enum Result { Error=-1, NoFrame=0, NewFrame=1 };
  bool begin() {
    address_=0;
    pinMode(42,OUTPUT); digitalWrite(42,HIGH); delay(300);
    if(!Wire.begin(3,2,100000)) return false;
    Wire.setTimeOut(20);
    for(uint8_t candidate : {uint8_t(0x5D),uint8_t(0x14)}) {
      pinMode(41,OUTPUT); digitalWrite(41,LOW);
      pinMode(21,OUTPUT); digitalWrite(21,candidate==0x5D ? LOW:HIGH);
      delay(20); digitalWrite(41,HIGH); delay(20);
      digitalWrite(21,LOW); delay(50); pinMode(21,INPUT); delay(30);
      address_=candidate;
      uint8_t id[4]{};
      if(readReg(0x8140,id,4) && id[0]=='9' && id[1]=='1' && id[2]=='1' && id[3]==0) {
        uint8_t size[4]{};
        if(!readReg(0x8146,size,4)) continue;
        width_=size[0]|(size[1]<<8); height_=size[2]|(size[3]<<8);
        if(width_==0 || height_==0) continue;
        Serial.printf("[TOUCH] GT911 addr=0x%02X sensor=%ux%u\n",address_,width_,height_);
        return writeReg(0x814E,0);
      }
    }
    address_=0;
    return false;
  }
  Result read(Frame& out) {
    if(!address_) return Error;
    uint8_t status=0;
    if(!readReg(0x814E,&status,1)) return Error;
    if(!(status&0x80)) return NoFrame;
    uint8_t count=status&15;
    if(count>5) { writeReg(0x814E,0); return Error; }
    uint8_t raw[40]{};
    if(count && !readReg(0x814F,raw,count*8)) return Error;
    Frame next{}; next.count=count;
    for(uint8_t i=0;i<count;++i) {
      const uint8_t* p=&raw[i*8];
      next.points[i]={p[0],uint16_t(p[1]|(p[2]<<8)),uint16_t(p[3]|(p[4]<<8)),uint16_t(p[5]|(p[6]<<8))};
      for(uint8_t j=0;j<i;++j) if(next.points[i].id==next.points[j].id) { writeReg(0x814E,0);return Error; }
      if(next.points[i].x>=width_ || next.points[i].y>=height_) { writeReg(0x814E,0);return Error; }
    }
    if(!writeReg(0x814E,0)) return Error;
    out=next;
    return NewFrame;
  }
  uint16_t sensorWidth() const {return width_;}
  uint16_t sensorHeight() const {return height_;}
  uint8_t address() const {return address_;}
private:
  uint8_t address_=0;
  uint16_t width_=0,height_=0;
  bool readReg(uint16_t reg,uint8_t* out,size_t size) {
    Wire.beginTransmission(address_); Wire.write(uint8_t(reg>>8));Wire.write(uint8_t(reg));
    if(Wire.endTransmission(false)!=0) return false;
    if(Wire.requestFrom(int(address_),int(size))!=size) {while(Wire.available()) Wire.read();return false;}
    for(size_t i=0;i<size;++i) out[i]=uint8_t(Wire.read());
    return true;
  }
  bool writeReg(uint16_t reg,uint8_t value) {
    Wire.beginTransmission(address_);Wire.write(uint8_t(reg>>8));Wire.write(uint8_t(reg));Wire.write(value);
    return Wire.endTransmission()==0;
  }
};
