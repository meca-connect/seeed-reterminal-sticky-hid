#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_INTERNAL=4, MALLOC_CAP_DMA=8;
inline void* heap_caps_malloc(size_t n,int) {return std::malloc(n);}
inline void heap_caps_free(void* p) {std::free(p);}
