#pragma once
#include <stddef.h>
#include "ptp_core.h"
namespace stickyptp {
// -1: no-new-data; -2: malformed. Never mutate output on malformed input.
inline int decodeFrame(uint8_t status,const uint8_t* raw,size_t bytes,Contact* out){
 if(!(status&0x80)) return -1;
 const unsigned n=status&15;
 if((status&0x70)||n>4||bytes!=n*8 || (n && !raw)) return -2;
 Contact tmp[4]{}; uint16_t ids=0;
 for(unsigned i=0;i<n;i++){
   const uint8_t* p=raw+i*8;
   const unsigned x=p[1]|(unsigned(p[2])<<8), y=p[3]|(unsigned(p[4])<<8);
   if(p[0]>15||x>479||y>799||(ids&(1u<<p[0]))||p[7]!=0) return -2;
   ids|=uint16_t(1u<<p[0]); tmp[i]={p[0],uint16_t(x),uint16_t(799-y)};
 }
 for(unsigned i=0;i<n;i++)out[i]=tmp[i];
 return int(n);
}
}
