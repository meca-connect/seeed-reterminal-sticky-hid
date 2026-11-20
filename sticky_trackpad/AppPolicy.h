#pragma once
#include <cstdint>
#include <algorithm>
namespace stickyapp {
struct WheelReport {int wheel,pan;};
inline WheelReport wheelDispatch(int horizontal,int vertical) {return {vertical,horizontal};}
struct XY {float x,y;};
// Provisional normalization, not a calibration claim. ROTATE cycles 4 axis mappings
// into the same logical 800x480 space. Corner tests establish the installed mapping.
inline XY mapRaw(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint8_t rot) {
 float nx=std::clamp(float(x)/std::max(1,int(w)-1),0.0f,1.0f);
 float ny=std::clamp(float(y)/std::max(1,int(h)-1),0.0f,1.0f);
 float a=nx,b=ny;
 switch(rot&3) {case 1:a=1-ny;b=nx;break;case 2:a=1-nx;b=1-ny;break;case 3:a=ny;b=1-nx;break;default:break;}
 return {a*799,b*479};
}
class ArmGate {
 bool armed_=false,waitRelease_=true;
public:
 void setArmed(bool v) {armed_=v;waitRelease_=true;}
 bool armed() const {return armed_;}
 void cancelUntilRelease() {waitRelease_=true;}
 bool permit(bool ready,uint8_t count) {
  if(!ready || !armed_) {waitRelease_=true;return false;}
  if(waitRelease_) {if(count) return false;waitRelease_=false;}
  return true;
 }
};
}
