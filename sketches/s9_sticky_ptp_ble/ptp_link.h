#pragma once
#include <stdint.h>
namespace stickyptp {
inline uint32_t intervalUs(uint16_t units){ return units>=6 ? uint32_t(units)*1250u : 7500u; }
struct Link {
 bool connected=false,bonded=false,encrypted=false,subscribed=false,touchReady=false;
 bool suspended=false,reportProtocol=true;
 uint16_t mtu=23,handle=0xffff,interval=6,latency=0;
 uint8_t mode=0,selective=3;
 uint32_t epoch=0,inputEpoch=0;
 bool transportReady()const{
   return connected && bonded && encrypted && subscribed && touchReady && mtu>=31 && !suspended && reportProtocol;
 }
 bool inputEnabled()const{return transportReady() && mode==3 && (selective&1);}
};
}
