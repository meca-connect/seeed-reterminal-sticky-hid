#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "gt911_frame.h"
#ifndef STICKY_PTP_I2C_HZ
#define STICKY_PTP_I2C_HZ 400000
#endif
#ifndef STICKY_PTP_I2C_TIMEOUT_MS
#define STICKY_PTP_I2C_TIMEOUT_MS 5
#endif
namespace stickyptp {
class Touch {
 public:
 uint8_t address=0;
 uint32_t polls=0,frames=0,invalid=0,errors=0;
 static void increment(uint32_t& n){if(n!=UINT32_MAX)n++;}
 bool readReg(uint16_t reg,uint8_t* p,size_t n){
   Wire.beginTransmission(address); Wire.write(uint8_t(reg>>8)); Wire.write(uint8_t(reg));
   if(Wire.endTransmission(false)!=0) return false;
   if(Wire.requestFrom(int(address),int(n))!=n){ while(Wire.available()) Wire.read();return false; }
   for(size_t i=0;i<n;i++)p[i]=uint8_t(Wire.read());
   return true;
 }
 bool ack(){ // the ONLY GT911 register write, including initialization
   Wire.beginTransmission(address); Wire.write(uint8_t(0x81));Wire.write(uint8_t(0x4e));Wire.write(uint8_t(0));
   return Wire.endTransmission()==0;
 }
 bool begin(){
   pinMode(42,OUTPUT);digitalWrite(42,HIGH);delay(300);
   if(!Wire.begin(3,2,STICKY_PTP_I2C_HZ))return false;
   Wire.setTimeOut(STICKY_PTP_I2C_TIMEOUT_MS);
   const uint8_t candidates[]={0x5d,0x14};
   for(uint8_t a:candidates){
     pinMode(41,OUTPUT);digitalWrite(41,LOW);
     pinMode(21,OUTPUT);digitalWrite(21,a==0x5d?LOW:HIGH);
     delay(20);digitalWrite(41,HIGH);delay(20);
     digitalWrite(21,LOW);delay(50);pinMode(21,INPUT);delay(30);
     address=a;uint8_t id[4]{};
     if(readReg(0x8140,id,4)&&id[0]=='9'&&id[1]=='1'&&id[2]=='1'&&id[3]==0&&ack())return true;
   }
   address=0;return false;
 }
 int poll(Contact* out){
   if(!address)return -1;
   increment(polls);uint8_t status=0;
   if(!readReg(0x814e,&status,1)){increment(errors);return -2;}
   if(!(status&0x80))return -1;
   const unsigned n=status&15;uint8_t raw[32]{};
   bool ok=n<=4 && !(status&0x70);
   if(ok && n && !readReg(0x814f,raw,n*8)){ok=false;increment(errors);}
   Contact tmp[4]{};
   int count=ok?decodeFrame(status,raw,n*8,tmp):-2;
   // Clear malformed ready frames too, but never publish unacknowledged data.
   if(!ack()){increment(errors);return -2;}
   if(count<0){increment(invalid);return -2;}
   for(int i=0;i<count;i++)out[i]=tmp[i];
   increment(frames);return count;
 }
};
}
