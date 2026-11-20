#include <cassert>
#include <cstdio>
#include "../sketches/s9_sticky_ptp_ble/ptp_link.h"
int main(){
 stickyptp::Link l;
 assert(!l.transportReady());
 l.connected=l.bonded=l.encrypted=l.subscribed=l.touchReady=true;
 l.mtu=30;assert(!l.transportReady());l.mtu=31;assert(l.transportReady());
 assert(!l.inputEnabled());l.mode=3;assert(l.inputEnabled());
 l.selective=2;assert(!l.inputEnabled());l.selective=1;assert(l.inputEnabled());
 l.suspended=true;assert(!l.transportReady());l.suspended=false;
 l.reportProtocol=false;assert(!l.transportReady());
 l.reportProtocol=true;l.encrypted=false;assert(!l.transportReady());
 l.encrypted=true;l.bonded=false;assert(!l.transportReady());
 l.bonded=true;l.subscribed=false;assert(!l.transportReady());
 assert(stickyptp::intervalUs(6)==7500 && stickyptp::intervalUs(12)==15000);
 assert(stickyptp::intervalUs(0)==7500);
 puts("PASS readiness, bonded encryption, MTU31, mode/selective/protocol/suspend, actual interval");
}
