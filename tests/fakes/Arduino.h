#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <map>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
inline uint32_t fakeMs = 0;
inline std::map<int,int> pinLevels;
inline uint32_t millis() { return fakeMs; }
inline void delay(uint32_t n) { fakeMs += n; }
inline void delayMicroseconds(unsigned) {}
inline void pinMode(int,int) {}
inline void digitalWrite(int p,int v) { pinLevels[p]=v; }
inline int digitalRead(int p) { return pinLevels[p]; }
struct FakeSerial {
 std::string output;
 void begin(int) {}
 explicit operator bool() const { return true; }
 void println(const char *p="") { output+=p; output+='\n'; }
 template<typename... T> void printf(const char *fmt,T... args) {
   char b[1024]; std::snprintf(b,sizeof(b),fmt,args...); output+=b;
 }
};
inline FakeSerial Serial;
