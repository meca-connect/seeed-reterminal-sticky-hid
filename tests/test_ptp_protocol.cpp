#include <cassert>
#include <cstdio>
#include "../sketches/s9_sticky_ptp_ble/ptp_features.h"
#include "../sketches/s9_sticky_ptp_ble/gt911_frame.h"
using namespace stickyptp;
int main(){
 Features f; uint8_t out[33]{},v=3;
 assert(f.read(RID_CAPS,out)==1 && out[0]==0x14);
 assert(f.write(RID_INPUT_MODE,&v,1)); assert(f.read(RID_INPUT_MODE,out)==1 && out[0]==3);
 v=2; assert(!f.write(RID_INPUT_MODE,&v,1)); assert(f.mode==3);
 v=0; assert(f.write(RID_SELECTIVE,&v,1) && f.selective==0);
 v=1; assert(f.write(RID_LATENCY,&v,1)); f.read(RID_LATENCY,out); assert(out[0]==1);
 for(uint8_t i=1;i<=8;i++){assert(f.write(RID_PTPHQA,&i,1)); assert(f.read(RID_PTPHQA,out)==33 && out[0]==i); for(int j=0;j<32;j++)assert(out[j+1]==ptphqaBlob[(i-1)*32+j]);}
 v=9; assert(!f.write(RID_PTPHQA,&v,1)); assert(!f.write(RID_CAPS,&v,1));
 f.reset(); assert(f.mode==0 && f.selective==3 && f.latency==0 && f.segment==1);
 uint8_t raw[32]{}; Contact c[4]{};
 raw[0]=4;raw[1]=0xdf;raw[2]=1;raw[3]=0x1f;raw[4]=3;
 assert(decodeFrame(0x81,raw,8,c)==1 && c[0].id==4 && c[0].x==479 && c[0].y==0);
 assert(decodeFrame(0,raw,8,c)==-1);
 assert(decodeFrame(0x80,nullptr,0,c)==0);
 assert(decodeFrame(0x85,raw,32,c)==-2);
 assert(decodeFrame(0x81,raw,7,c)==-2);
 raw[1]=0xe0;assert(decodeFrame(0x81,raw,8,c)==-2);raw[1]=0;
 raw[3]=0x20;assert(decodeFrame(0x81,raw,8,c)==-2);raw[3]=0;
 raw[0]=16;assert(decodeFrame(0x81,raw,8,c)==-2);raw[0]=0;
 assert(decodeFrame(0x82,raw,16,c)==-2);
 assert(decodeFrame(0x91,raw,8,c)==-2);
 puts("PASS feature readback/validation/reset/HQA, actual frame decoding/malformed checks");
}
