#pragma once
#include <stddef.h>
#include "ptp_descriptor.h"
namespace stickyptp {
// GATT values exclude HID report ID (ID lives in Report Reference descriptor).
struct Features {
 uint8_t mode=0,selective=3,latency=0,segment=1;
 void reset(){ mode=0;selective=3;latency=0;segment=1; }
 bool write(uint8_t id,const uint8_t* p,size_t n){
   if(!p || (id==RID_PTPHQA ? (n!=1 && n!=33) : n!=1)) return false;
   const auto v=p[0];
   switch(id){
    case RID_INPUT_MODE:if(v!=0 && v!=3)return false;mode=v;return true;
    case RID_SELECTIVE:if(v>3)return false;selective=v;return true;
    case RID_LATENCY:if(v>1)return false;latency=v;return true;
    case RID_PTPHQA:if(v<1||v>8)return false;segment=v;return true;
    default:return false;
   }
 }
 size_t read(uint8_t id,uint8_t* p)const{
   switch(id){
    case RID_CAPS:p[0]=0x14;return 1; // four contacts, non-depressible glass
    case RID_INPUT_MODE:p[0]=mode;return 1;
    case RID_SELECTIVE:p[0]=selective;return 1;
    case RID_LATENCY:p[0]=latency;return 1;
    case RID_PTPHQA:p[0]=segment;for(unsigned i=0;i<32;i++)p[i+1]=ptphqaBlob[(segment-1)*32+i];return 33;
    default:return 0;
   }
 }
};
}
