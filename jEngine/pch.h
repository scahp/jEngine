#ifndef PCH_H
#define PCH_H

#define NOMINMAX

#include <windows.h>

#define API_TYPE 1

#if (API_TYPE == 1)
#define USE_VULKAN 1
#elif (API_TYPE == 2)
#define USE_OPENGL 1
#endif

#ifndef USE_VULKAN
#define USE_VULKAN 0
#endif

#ifndef USE_OPENGL
#define USE_OPENGL 0
#endif

#define LEFT_HANDED !USE_OPENGL
#define RIGHT_HANDED USE_OPENGL

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
#include <mutex>

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

#include "External/cityhash/city.h"

#define JASSERT(x) assert(x)
#define JMESSAGE(x) MessageBoxA(0, x, "", MB_OK)

#define ensure(x) (((x) || (JASSERT(0), 0)))
#define check(x) JASSERT(x)
#if _DEBUG
#define verify(x) JASSERT(x)
#else
#define verify(x) (x)
#endif

#include "RHI/jRHIType.h"
#include "RHI/jRHI.h"

#if USE_VULKAN
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#define GLFW_INCLUDE_VULKAN
#include "imgui_impl_vulkan.h"
#include "RHI/jRHI_Vulkan.h"
#include "Shader/Spirv/jSpirvHelper.h"
#elif USE_OPENGL
#include <GLFW/glfw3.h>
#include "jRHI_OpenGL.h"
#include "IMGUI/imgui_impl_opengl3.h"
#else
#endif

// imgui
#include "IMGUI/imgui.h"
#include "IMGUI/imgui_impl_glfw.h"

#include "ImGui/jImGui.h"

// settings
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

const unsigned int SM_WIDTH = 1024;
const unsigned int SM_HEIGHT = 1024;

const unsigned int SM_ARRAY_WIDTH = 512;
const unsigned int SM_ARRAY_HEIGHT = 512;

const unsigned int SM_LINKED_LIST_WIDTH = SM_WIDTH;
const unsigned int SM_LINKED_LIST_HEIGHT = SM_HEIGHT;

const unsigned int LUMINANCE_WIDTH = 512;
const unsigned int LUMINANCE_HEIGHT = 512;

constexpr int NUM_CASCADES = 3;
constexpr int NUM_FRUSTUM_CORNERS = 8;

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
	static int64 lastTick = GetTickCount64();\
	int64 currentTick = GetTickCount64();\
	if (currentTick - lastTick >= waitMS)\
	{\
		lastTick = currentTick;\
		return true;\
	}\
	return false;\
}()

#define DEBUG_OUTPUT_ON 0
//#define DEBUG_OUTPUT_LEVEL 0	// show all
//#define DEBUG_OUTPUT_LEVEL 1	// show mid priority
#define DEBUG_OUTPUT_LEVEL 2	// show high priority

#include "Core/jName.h"

// string type city hash generator
#define STATIC_NAME_CITY_HASH(str) []() -> size_t { \
			static size_t StrLen = strlen(str); \
			static size_t hash = CityHash64(str, StrLen); \
			return hash;\
		}();

extern const uint32 MaxQueryTimeCount;

template <typename T>
FORCEINLINE constexpr T Align(T value, uint64 alignment)
{
	static_assert(std::is_integral<T>::value || std::is_pointer<T>::value, "Align is support for int or pointer type");
    return (T)(((uint64)value + alignment - 1) & ~(alignment - 1));
}

#endif //PCH_H
