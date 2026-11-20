#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#include <utility>
inline uint32_t fakeTime = 0;
inline std::map<int,int> fakePins;
inline bool stuckBusy = false, busyEnabled = true;
inline uint32_t busyUntil = 0;
constexpr int HIGH=1, LOW=0, INPUT=0, OUTPUT=1;
inline void pinMode(int,int) {}
inline void digitalWrite(int p,int v) { fakePins[p]=v; }
inline int digitalRead(int p) {
 if(p==18) return stuckBusy || (busyEnabled && int32_t(busyUntil-fakeTime)>0);
 return fakePins[p];
}
inline uint32_t millis() { return fakeTime; }
inline void delay(uint32_t t) {fakeTime+=t;}
struct SerialFake {
 template<class... Args> void printf(const char*,Args...) {}
 void println(const char*) {}
};
inline SerialFake Serial;
