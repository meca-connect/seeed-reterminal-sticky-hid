#include <cassert>
#include <cstdio>
#include <map>
#include "../sketches/s9_sticky_ptp_ble/ptp_descriptor.h"
int main(){
  assert(sizeof(ptpReportDescriptor)<=512);
  unsigned size=0,count=0,id=0,page=0,usage=0,max=0,phys=0,fingers=0;
  std::map<unsigned,unsigned> input,feature;
  unsigned xs=0,ys=0;
  for(size_t i=0;i<sizeof(ptpReportDescriptor);){
    unsigned b=ptpReportDescriptor[i++],n=b&3; if(n==3)n=4;
    assert(b!=0xfe && i+n<=sizeof(ptpReportDescriptor));
    unsigned v=0; for(unsigned j=0;j<n;j++)v|=unsigned(ptpReportDescriptor[i++])<<(8*j);
    switch(b&0xfc){
      case 0x04:page=v;break; case 0x08:usage=v;break;
      case 0x24:max=v;break; case 0x44:phys=v;break;
      case 0x74:size=v;break;case 0x94:count=v;break;case 0x84:id=v;break;
      case 0xa0:if(page==13&&usage==0x22&&v==2) fingers++;break;
      case 0x80:
        input[id]+=size*count;
        if(page==1&&usage==0x30){assert(max==958 && phys==STICKY_PTP_WIDTH_001IN); xs++;}
        if(page==1&&usage==0x31){assert(max==1598 && phys==STICKY_PTP_HEIGHT_001IN); ys++;}
        break;
      case 0xb0:feature[id]+=size*count;break;
    }
  }
  assert(fingers==5); // four input fingers plus configuration finger logical collection
  assert(xs==4 && ys==4 && input[RID_TOUCHPAD]==224);
  assert(feature[RID_CAPS]==8 && feature[RID_PTPHQA]==264);
  assert(feature[RID_INPUT_MODE]==8 && feature[RID_SELECTIVE]==8 && feature[RID_LATENCY]==8);
  printf("PASS descriptor bytes=%zu input=28 bytes features=1/33/1/1/1 ranges=958x1598\n",sizeof(ptpReportDescriptor));
}
