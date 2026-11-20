#include <cassert>
#include <cstdio>
#include "../sketches/s9_sticky_ptp_ble/ptp_core.h"
using namespace stickyptp;
int main() {
  Core c; Contact p[4]={{0,0,0},{1,479,799},{2,100,200},{3,200,300}};
  c.reset(0); c.setEnabled(true,0);
  assert(c.sample(p,4,400000));
  Report r{}; assert(c.prepare(400000,7500,r));
  assert(r.contactCount==4 && r.c[1].x==958 && r.c[1].y==1598);
  c.complete(true,400000);
  assert(!c.prepare(400001,7500,r));
  assert(c.prepare(407500,7500,r));
  assert(r.contactCount==4);
  c.complete(true,407500);
  Contact q[4]={{4,20,30},{5,30,40},{6,40,50},{7,50,60}};
  assert(c.sample(q,4,408000));
  assert(c.prepare(415000,7500,r));
  assert(r.contactCount==4);
  for(auto &v:r.c) assert(v.flags==1 && v.id<4);
  c.complete(false,415000); // no commit on failure
  assert(c.sample(q,4,416000));
  assert(c.prepare(422500,7500,r));
  for(auto &v:r.c) assert(v.flags==1 && v.id<4);
  c.complete(true,422500);
  assert(c.prepare(430000,7500,r));
  for(auto &v:r.c) assert(v.flags==3 && v.id>=4);
  c.complete(true,430000);
  assert(c.sample(nullptr,0,431000));
  assert(c.sample(q,4,432000)); // coalescing must retain observed up transitions
  assert(c.prepare(437500,7500,r));
  for(auto &v:r.c) assert(v.flags==1);
  c.complete(true,437500);
  assert(c.prepare(445000,7500,r));
  for(auto &v:r.c) assert(v.flags==3);
  c.complete(true,445000);
  Contact bad{16,0,0}; assert(!c.sample(&bad,1,446000));
  bad={1,480,0}; assert(!c.sample(&bad,1,446000));
  bad={1,0,800}; assert(!c.sample(&bad,1,446000));
  Contact dup[2]={{1,0,0},{1,10,10}}; assert(!c.sample(dup,2,446000));
  assert(!c.sample(nullptr,5,446000));
  assert(!c.sample(nullptr,1,446000));
  // No call to sample represents no-new-data; keep held until stale.
  assert(c.prepare(452500,7500,r)); for(auto &v:r.c) assert(v.flags==3);
  c.complete(true,452500);
  assert(c.prepare(532001,7500,r)); for(auto &v:r.c) assert(v.flags==1);
  c.complete(true,532001); assert(!c.prepare(540000,7500,r));
  c.reset(600000); c.setEnabled(true,600000);
  assert(!c.sample(q,4,999999)); assert(c.sample(q,4,1000000));
  assert(c.prepare(1000000,7500,r)); c.complete(true,1000000);
  c.setEnabled(false,1000100); assert(c.prepare(1007500,7500,r));
  for(auto &v:r.c) { assert(v.flags==1); }
  c.complete(true,1007500);
  assert(!c.sample(q,4,2000000));
  c.reset(0xffff0000u); c.setEnabled(true,0xffff0000u);
  uint32_t wrapped=uint32_t(0xffff0000u+400000u);
  assert(c.sample(q,4,wrapped)); assert(c.prepare(wrapped,7500,r)); c.complete(true,wrapped);
  assert(!c.prepare(wrapped+7499,7500,r));
  assert(c.prepare(wrapped+7500,7500,r)); c.complete(true,wrapped+7500);
  assert(c.prepare(wrapped+200000,7500,r)); c.complete(true,wrapped+200000);
  assert(!c.prepare(wrapped+200001,7500,r)); // no catch-up flood
  // Stable survivor ordering; missing one ID emits its final coordinates first.
  c.reset(0);c.setEnabled(true,0);assert(c.sample(p,4,400000));
  assert(c.prepare(400000,15000,r));c.complete(true,400000);
  Contact reversed[4]={p[3],p[2],p[1],p[0]};assert(c.sample(reversed,4,401000));
  assert(c.prepare(415000,15000,r));for(unsigned i=0;i<4;i++)assert(r.c[i].id==i);
  c.complete(true,415000);
  assert(c.sample(reversed,3,416000));assert(c.prepare(430000,15000,r));
  assert(r.c[0].id==0 && r.c[0].flags==1 && r.contactCount==4);
  c.complete(true,430000);
  assert(c.sample(nullptr,0,431000));assert(c.prepare(445000,15000,r));assert(r.contactCount==3);
  for(unsigned i=0;i<3;i++)assert(r.c[i].flags==1);
  c.complete(true,445000);assert(!c.prepare(460000,15000,r));
  puts("PASS core: four contacts, reorder, partial/full lifts, replacement, coalesced ups, notify failure, malformed, stale, disable, reconnect, wrap/pacing/no-burst");
}
