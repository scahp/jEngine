#pragma once

/*!
 * \file CoreDefines.h
 * \brief Core defines and configuration macros for the engine
 */

#include <assert.h>

// SIMD Configuration
//////////////////////////////////////////////////////////////////////////

// SSE (Streaming SIMD Extensions) support
// Default: enabled on x86/x64 targets that advertise SSE, disabled elsewhere.
#if !defined(SUPPORT_SSE)
	#if defined(_M_X64) || defined(__x86_64__) \
		|| (defined(_M_IX86_FP) && _M_IX86_FP >= 1) || defined(__SSE__)
		#define SUPPORT_SSE 1
	#else
		#define SUPPORT_SSE 0
	#endif
#endif

#define USE_SSE (0 && SUPPORT_SSE)
#define USE_SSE_VECTOR 0		// Not enough fast than NoSSE
#define USE_SSE_MATRIX 1

// Fine-grained SIMD toggles (default: follow USE_SSE)
#if !defined(USE_SSE_VECTOR)
	#define USE_SSE_VECTOR USE_SSE
#endif

#if !defined(USE_SSE_MATRIX)
	#define USE_SSE_MATRIX USE_SSE
#endif

#if USE_SSE
	#include <xmmintrin.h>  // SSE
	#include <emmintrin.h>  // SSE2
	#include <pmmintrin.h>  // SSE3
	#include <smmintrin.h>  // SSE4.1
#endif

//////////////////////////////////////////////////////////////////////////
// Basic Type Definitions
//////////////////////////////////////////////////////////////////////////

using int8 = char;
using uint8 = unsigned char;
using int16 = short;
using uint16 = unsigned short;
using int32 = int;
using uint32 = unsigned int;
using int64 = long long;
using uint64 = unsigned long long;

//////////////////////////////////////////////////////////////////////////
// Inline / Assert helpers
//////////////////////////////////////////////////////////////////////////

#ifndef FORCEINLINE
	#if defined(_MSC_VER)
		#define FORCEINLINE __forceinline
	#else
		#define FORCEINLINE inline __attribute__((always_inline))
	#endif
#endif

#ifndef JASSERT
	#if _DEBUG
		#define verify(x) JASSERT(x)
		#define JOK(a) (SUCCEEDED(a) ? true : (assert(!(#a)), false))
		#define JFAIL(a) (!JOK(a))
		#define JOK_E(a, errorBlob) (SUCCEEDED(a) ? true : [&errorBlob](){ if (errorBlob) {OutputDebugStringA((const char*)errorBlob->GetBufferPointer());} assert(!#a); return false; }())
		#define JFAIL_E(a, errorBlob) (!JOK_E(a, errorBlob))
		#define JASSERT(a) ((a) ? true : (assert(!(#a)), false))
		#define JMESSAGE(x) MessageBoxA(0, x, "", MB_OK)
		#define check(x) JASSERT(x)
		#define ensure(x) (((x) || (assert(!(#x)), false)))
	#else
		#define verify(x) (x)
		#define JOK(a) (SUCCEEDED(a))
		#define JFAIL(a) (!JOK(a))
		#define JOK_E(a, errorBlob) (SUCCEEDED(a))
		#define JFAIL_E(a, errorBlob) (!JOK_E(a, errorBlob))
		#define JASSERT(a) (a)
		#define JMESSAGE(a) (a)
		#define check(x)
		#define ensure(x) (x)
	#endif
#endif

//////////////////////////////////////////////////////////////////////////
// Math Configuration
//////////////////////////////////////////////////////////////////////////

// Coordinate system handedness
#define LEFT_HANDED 1
#define RIGHT_HANDED !LEFT_HANDED

// Editor Features
// Uncomment to enable editor-specific features like Placement Tool
#define ENABLE_EDITOR_FEATURES

//////////////////////////////////////////////////////////////////////////
// Profiler Backend Configuration
//////////////////////////////////////////////////////////////////////////

#define JPROFILE_BACKEND_LEGACY 0
#define JPROFILE_BACKEND_TRACY 1

// Default profiler backend:
// 0: legacy in-engine profiler
// 1: Tracy
#ifndef JPROFILE_BACKEND
#define JPROFILE_BACKEND JPROFILE_BACKEND_TRACY
#endif

// Tracy system tracing controls automated OS-level data collection
// (context switches / CPU data). Enable for richer CPU analysis.
// Set to 0 if your local toolchain hits Tracy+MSVC debug build issues.
#ifndef JPROFILE_TRACY_ENABLE_SYSTEM_TRACING
#define JPROFILE_TRACY_ENABLE_SYSTEM_TRACING 1
#endif

//////////////////////////////////////////////////////////////////////////
// Debug Configuration
//////////////////////////////////////////////////////////////////////////

// Add more debug/configuration defines here as needed
// Example:
// #ifndef USE_CUSTOM_ALLOCATOR
//     #define USE_CUSTOM_ALLOCATOR 0
// #endif

//////////////////////////////////////////////////////////////////////////
