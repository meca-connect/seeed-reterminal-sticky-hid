#pragma once
#include <stdint.h>
namespace stickyptp {
struct Contact { uint8_t id; uint16_t x,y; };
struct __attribute__((packed)) WireContact { uint8_t flags,id; uint16_t x,y; };
struct __attribute__((packed)) Report { WireContact c[4]; uint16_t scanTime; uint8_t contactCount,buttons; };
static_assert(sizeof(WireContact)==6 && sizeof(Report)==28,"HID wire layout");
class Core {
 public:
 void reset(uint32_t now) {
   n_=sentN_=stagedN_=0; retire_=stagedLifts_=0; enabled_=false;
   gate_=lastSample_=lastAttempt_=now; attempted_=false;
 }
 void setEnabled(bool on,uint32_t now) {
   enabled_=on; gate_=now; n_=0;
   for(unsigned i=0;i<sentN_;i++) retire_|=uint16_t(1u<<sent_[i].id);
 }
 bool sample(const Contact* p,uint8_t n,uint32_t now) {
   if (!enabled_ || uint32_t(now-gate_)<400000 || n>4 || (n && !p)) return false;
   uint16_t seen=0;
   for(unsigned i=0;i<n;i++) {
     if(p[i].id>15 || p[i].x>479 || p[i].y>799 || (seen & (1u<<p[i].id))) return false;
     seen |= uint16_t(1u<<p[i].id);
   }
   for(unsigned j=0;j<sentN_;j++) {
     bool present=false;
     for(unsigned i=0;i<n;i++) if(p[i].id==sent_[j].id) present=true;
     if(!present) retire_|=uint16_t(1u<<sent_[j].id);
   }
   n_=n; for(unsigned i=0;i<n;i++) current_[i]=p[i];
   lastSample_=now; return true;
 }
 bool prepare(uint32_t now,uint32_t interval,Report& r) {
   if (n_ && uint32_t(now-lastSample_)>100000) {
     n_=0;
     for(unsigned i=0;i<sentN_;i++) retire_|=uint16_t(1u<<sent_[i].id);
   }
   if (attempted_ && uint32_t(now-lastAttempt_)<interval) return false;
   if (!n_ && !sentN_) return false;
   r={}; stagedN_=0; stagedLifts_=0; r.scanTime=uint16_t(now/100);
   // Retire before admitting replacements: even four simultaneous replacements fit.
   for(unsigned j=0;j<sentN_;j++) if(retire_ & (1u<<sent_[j].id)) {
     auto p=sent_[j]; r.c[r.contactCount++]={1,p.id,uint16_t(p.x*2),uint16_t(p.y*2)};
     stagedLifts_|=uint16_t(1u<<p.id);
   }
   // Keep survivor slots stable under GT911 record reordering, then admit new IDs.
   for(unsigned pass=0;pass<2;pass++) for(unsigned i=0;i<(pass? n_:sentN_);i++) {
     Contact p{}; bool found=false;
     if(!pass) {
       for(unsigned k=0;k<n_;k++) if(current_[k].id==sent_[i].id) { p=current_[k]; found=true; }
     } else {
       p=current_[i]; found=true;
       for(unsigned j=0;j<sentN_;j++) if(sent_[j].id==p.id) found=false;
     }
     if(!found || (retire_ & (1u<<p.id)) || r.contactCount==4) continue;
     r.c[r.contactCount++]={3,p.id,uint16_t(p.x*2),uint16_t(p.y*2)};
     staged_[stagedN_++]=p;
   }
   attempted_=true; lastAttempt_=now; return true;
 }
 void complete(bool success,uint32_t) {
   if(!success) return;
   sentN_=stagedN_; for(unsigned i=0;i<sentN_;i++) sent_[i]=staged_[i];
   retire_ &= uint16_t(~stagedLifts_);
 }
 private:
 Contact current_[4]{},sent_[4]{},staged_[4]{};
 uint8_t n_=0,sentN_=0,stagedN_=0; uint16_t retire_=0,stagedLifts_=0;
 bool enabled_=false,attempted_=false;
 uint32_t gate_=0,lastSample_=0,lastAttempt_=0;
};
}
