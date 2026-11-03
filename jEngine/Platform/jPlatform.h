#pragma once
// Small compatibility surface for CRT calls shared with the Windows build.
#include <algorithm>
#include <cerrno>
#include <cstdio>
#define __forceinline inline __attribute__((always_inline))
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cstdarg>
#include <thread>
#include <chrono>
#include <alloca.h>
#include <cmath>
#include <cfloat>
#include <climits>
#include <atomic>
#define TEXT(x) L##x
#define TRUE 1
#define FALSE 0
#define _countof(x) (sizeof(x) / sizeof((x)[0]))
#define _alloca alloca
#include "jSecureScanf.h"
#define strtok_s strtok_r
inline void OutputDebugStringA(const char* s) { std::fputs(s, stderr); }
inline void Sleep(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline int fopen_s(FILE** f, const char* p, const char* mode) { *f = std::fopen(p, mode); return *f ? 0 : errno; }
template<class... A> int sprintf_s(char* dst, size_t n, const char* fmt, A... args) { return std::snprintf(dst, n, fmt, args...); }
template<size_t N, class... A> int sprintf_s(char (&dst)[N], const char* fmt, A... args) { return std::snprintf(dst, N, fmt, args...); }
inline int strncpy_s(char* dst, size_t n, const char* src, size_t count) { if (!n) return EINVAL; const auto len = std::min(std::min(count, n - 1), std::strlen(src)); std::memcpy(dst, src, len); dst[len] = 0; return 0; }
inline void* _aligned_malloc(size_t n, size_t a) { void* p = nullptr; return posix_memalign(&p, std::max(a, sizeof(void*)), n) == 0 ? p : nullptr; }
inline void _aligned_free(void* p) { std::free(p); }

#include <strings.h>
#define _stricmp strcasecmp

template<size_t N, class... A> int wsprintf(wchar_t (&dst)[N], const wchar_t* fmt, A... args) { return std::swprintf(dst, N, fmt, args...); }
