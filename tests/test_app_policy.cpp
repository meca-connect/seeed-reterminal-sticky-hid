// Pure policy tests use synthetic contacts, not hardware samples.
#include <cassert>
#include <cstdio>
#include "../sticky_trackpad/AppPolicy.h"
int main() {
 using namespace stickyapp;
 auto a=mapRaw(0,0,480,800,0); auto b=mapRaw(479,799,480,800,0);
 auto scroll=wheelDispatch(3,-2);assert(scroll.wheel==-2 && scroll.pan==3);
 assert(a.x==0 && a.y==0 && b.x==799 && b.y==479);
 auto r=mapRaw(0,0,480,800,1); assert(r.x==799 && r.y==0);
 ArmGate gate;
 assert(!gate.permit(true,0));
 gate.setArmed(true); assert(!gate.permit(true,1)); // must see full release first
 assert(gate.permit(true,0)); assert(gate.permit(true,3));
 assert(!gate.permit(false,0));
 assert(!gate.permit(true,2)); // reconnect must not act on already-held fingers
 assert(gate.permit(true,0));
 gate.setArmed(false); assert(!gate.permit(true,0));
 puts("PASS coordinate endpoints/rotation and arm/reconnect release gating");
}
