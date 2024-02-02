#ifndef PCH_H
#define PCH_H

#define NOMINMAX

#include <windows.h>

#define LEFT_HANDED 1
#define RIGHT_HANDED !LEFT_HANDED

using int8 = char;
using uint8 = unsigned char;
using int16 = short;
using uint16 = unsigned short;
using int32 = int;
using uint32 = unsigned int;
using int64 = __int64;
using uint64 = unsigned __int64;
using tchar = wchar_t;

#include <EASTL/vector.h>
#include <EASTL/list.h>
#include <EASTL/map.h>
#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/set.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>

#include <assert.h>
#include <vector>
#include <list>
#include <map>
#include <array>
#include <string>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <future>
#include <mutex>
#include <shared_mutex>

#include <memory>
#include <stdexcept>
#include <stdlib.h>
#include <functional>
#include <sstream>
#include <algorithm>
#include <limits>
#include <type_traits>
#include <iostream>
#include <optional>
#include <chrono>
#include <fstream>
#include <execution>
#include <ppl.h>
#include <filesystem>

#include "External/cityhash/city.h"
#include "External/robin-hood-hashing/robin_hood.h"

#include "External/xxHash/xxhash.h"
template <typename T>
FORCEINLINE uint64 XXH64(const T& InData, uint64 InSeed = 0)
{
	static_assert(std::is_trivially_copyable<T>::value, "Custom XXH64 function Should be trivially copyable.");
	static_assert(!std::is_pointer<T>::value, "Custom XXH64 function is Not allowed pointer type.");
	return XXH64(&InData, sizeof(T), InSeed);
}

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

#include "Math/MathUtility.h"

#include "RHI/jRHIType.h"
#include "RHI/jRHI.h"

//////////////////////////////////////////////////////////////////////////
// Vulkan
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#define GLFW_INCLUDE_VULKAN
#include "imgui_impl_vulkan.h"
#include "RHI/Vulkan/jRHI_Vulkan.h"
#include "Shader/Spirv/jSpirvHelper.h"
#include "RHI/Vulkan/jVulkanFeatureSwitch.h"

// DX12
#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include "IMGUI/imgui_impl_win32.h"
#include "IMGUI/imgui_impl_dx12.h"
#include "RHI/DX12/jRHI_DX12.h"
//////////////////////////////////////////////////////////////////////////

// imgui
#include "IMGUI/imgui.h"
#include "IMGUI/imgui_impl_glfw.h"

#include "ImGui/jImGui.h"

// settings
extern int32 SCR_WIDTH;
extern int32 SCR_HEIGHT;
extern bool IsSizeMinimize;

const uint32 SM_WIDTH = 1024;
const uint32 SM_HEIGHT = 1024;

const uint32 SM_ARRAY_WIDTH = 512;
const uint32 SM_ARRAY_HEIGHT = 512;

const uint32 SM_LINKED_LIST_WIDTH = SM_WIDTH;
const uint32 SM_LINKED_LIST_HEIGHT = SM_HEIGHT;

const uint32 LUMINANCE_WIDTH = 512;
const uint32 LUMINANCE_HEIGHT = 512;

constexpr int32 NUM_CASCADES = 3;
constexpr int32 NUM_FRUSTUM_CORNERS = 8;

#define FORCEINLINE __forceinline

enum class EMouseButtonType
{
	LEFT = 0,
	MIDDLE,
	RIGHT,
	MAX
};

extern std::map<int, bool> g_KeyState;
extern std::map<EMouseButtonType, bool> g_MouseState;
extern float g_timeDeltaSecond;

#define TRUE_PER_MS(WaitMS)\
[waitMS = WaitMS]() -> bool\
{\
	static std::chrono::system_clock::time_point lastTime = std::chrono::system_clock::now();\
	const std::chrono::system_clock::time_point currentTime = std::chrono::system_clock::now();\
	const std::chrono::milliseconds MS = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastTime);\
	if (MS >= waitMS)\
	{\
		lastTime = currentTime;\
		return true;\
	}\
	return false;\
}()

#define DEBUG_OUTPUT_ON 0
//#define DEBUG_OUTPUT_LEVEL 0	// show all
//#define DEBUG_OUTPUT_LEVEL 1	// show mid priority
#define DEBUG_OUTPUT_LEVEL 2	// show high priority

#include "Core/jLock.h"
#include "Core/jName.h"
#include "Core/TInstantStruct.h"

template <typename T>
FORCEINLINE constexpr T Align(T value, uint64 alignment)
{
	static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "Align is support for int or pointer type");
    return (T)(((uint64)value + alignment - 1) & ~(alignment - 1));
}

std::wstring ConvertToWchar(const char* InPath, int32 InLength);

FORCEINLINE std::wstring ConvertToWchar(jName InName)
{
	check(InName.IsValid());
	return ConvertToWchar(InName.ToStr(), (int32)InName.GetStringLength());
}

#include "Core/jMemStackAllocator.h"
#include "Core/jParallelFor.h"

extern uint32 GetMaxThreadCount();

#define USE_PIX 1
#if USE_PIX
#include "pix3.h"
#endif

enum class EAPIType : uint8
{
	None,
	Vulkan,
	DX12
};

extern EAPIType gAPIType;
extern bool IsUseVulkan();
extern bool IsUseDX12();

extern class jEngine* g_Engine;

#define ENABLE_PBR 1
#define USE_SPONZA 1
#define USE_SPONZA_PBR 1
#define SUPPORT_RAYTRACING 0

extern bool GUseRealTimeShaderUpdate;
extern int32 GMaxCheckCountForRealTimeShaderUpdate;
extern int32 GSleepMSForRealTimeShaderUpdate;

extern std::thread::id GMainThreadID;
extern bool IsMainThread();
extern bool IsMainThread(const std::thread::id& InThreadId);

extern bool GRHISupportVsync;
extern bool GUseVsync;

#endif //PCH_H
