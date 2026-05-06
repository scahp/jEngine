#ifndef PCH_H
#define PCH_H

#define NOMINMAX

#include <windows.h>

#include "CoreDefines.h"

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
#include <random>

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
#include <D3DX12/d3dx12.h>
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

enum class EMouseButtonType
{
	LEFT = 0,
	MIDDLE,
	RIGHT,
	MAX
};

enum class EInputKey : uint16
{
	UNKNOWN = 0,

	A, B, C, D, E, F, G, H, I, J, K, L, M,
	N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

	NUM0, NUM1, NUM2, NUM3, NUM4, NUM5, NUM6, NUM7, NUM8, NUM9,

	PLUS,
	MINUS,

	SHIFT,
	CTRL,
	ALT,
	DELETE_KEY,
	ESCAPE,
	SPACE,
	ENTER,
	TAB,

	LEFT,
	RIGHT,
	UP,
	DOWN,

	MAX
};

FORCEINLINE constexpr size_t MouseButtonIndex(EMouseButtonType InButton)
{
	return static_cast<size_t>(InButton);
}

FORCEINLINE constexpr size_t InputKeyIndex(EInputKey InKey)
{
	return static_cast<size_t>(InKey);
}

extern uint64 g_MouseClickMaxDurationMS;
extern uint64 g_MouseLongPressMinDurationMS;
extern uint64 g_MouseDoubleClickMaxDurationMS;
extern int32 g_MouseDragThresholdPx;
extern int32 g_MouseDoubleClickMaxDistancePx;

struct jMouseButtonState
{
	bool Down = false;
	bool Up = true;

	// One-frame event flags
	bool PressedThisFrame = false;		// !Down -> Down edge
	bool ReleasedThisFrame = false;		// Down -> !Down edge
	bool Clicked = false;				// ReleasedThisFrame && short click && !Dragged
	bool DoubleClicked = false;			// Clicked and within double-click threshold
	bool DragStartedThisFrame = false;	// Drag threshold crossed this frame
	bool DragEndedThisFrame = false;	// Released after drag
	bool LongPressedThisFrame = false;	// Released and press duration >= long-press threshold

	// Continuous state
	bool Dragged = false;
	int32 PressedX = 0;					// Mouse position at button down
	int32 PressedY = 0;
	uint64 PressedTimeMS = 0;			// Timestamp at button down

	// Double click tracking
	bool HasLastClick = false;
	int32 LastClickX = 0;
	int32 LastClickY = 0;
	uint64 LastClickTimeMS = 0;

	FORCEINLINE void ResetFrameEvents()
	{
		PressedThisFrame = false;
		ReleasedThisFrame = false;
		Clicked = false;
		DoubleClicked = false;
		DragStartedThisFrame = false;
		DragEndedThisFrame = false;
		LongPressedThisFrame = false;
	}

	FORCEINLINE void ResetAll()
	{
		*this = jMouseButtonState();
	}

	void SetDownState(bool InDown, int32 InMouseX, int32 InMouseY, uint64 InEventTimeMS)
	{
		if (InDown)
		{
			if (!Down)
			{
				PressedX = InMouseX;
				PressedY = InMouseY;
				PressedTimeMS = InEventTimeMS;
				PressedThisFrame = true;
				Dragged = false;
			}
			Down = true;
			Up = false;
		}
		else
		{
			if (Down)
			{
				ReleasedThisFrame = true;
				DragEndedThisFrame = Dragged;

				const uint64 PressDurationMS = (InEventTimeMS >= PressedTimeMS) ? (InEventTimeMS - PressedTimeMS) : 0;
				const bool IsShortClick = (!Dragged && (PressDurationMS <= g_MouseClickMaxDurationMS));

				Clicked = IsShortClick;
				LongPressedThisFrame = (!Dragged && (PressDurationMS >= g_MouseLongPressMinDurationMS));

				if (Clicked)
				{
					if (HasLastClick)
					{
						const uint64 ClickDeltaMS = (InEventTimeMS >= LastClickTimeMS) ? (InEventTimeMS - LastClickTimeMS) : std::numeric_limits<uint64>::max();
						const int32 dx = InMouseX - LastClickX;
						const int32 dy = InMouseY - LastClickY;
						const int32 DistanceSq = dx * dx + dy * dy;
						const int32 MaxDistanceSq = g_MouseDoubleClickMaxDistancePx * g_MouseDoubleClickMaxDistancePx;
						DoubleClicked = (ClickDeltaMS <= g_MouseDoubleClickMaxDurationMS) && (DistanceSq <= MaxDistanceSq);
					}

					LastClickX = InMouseX;
					LastClickY = InMouseY;
					LastClickTimeMS = InEventTimeMS;
					HasLastClick = true;
				}
			}

			Down = false;
			Up = true;
			Dragged = false;
		}
	}

	void UpdateDragState(int32 InMouseX, int32 InMouseY)
	{
		if (!Down || Dragged)
			return;

		const int32 dx = InMouseX - PressedX;
		const int32 dy = InMouseY - PressedY;
		const int32 distanceSq = dx * dx + dy * dy;
		const int32 DragThresholdSq = g_MouseDragThresholdPx * g_MouseDragThresholdPx;
		if (distanceSq >= DragThresholdSq)
		{
			Dragged = true;
			DragStartedThisFrame = true;
		}
	}
};

extern std::array<bool, static_cast<size_t>(EInputKey::MAX)> g_KeyState;
extern std::array<jMouseButtonState, static_cast<size_t>(EMouseButtonType::MAX)> g_MouseState;
extern float g_timeDeltaSecond;
extern int32 g_MousePosX;
extern int32 g_MousePosY;

FORCEINLINE bool IsKeyDown(EInputKey InKey)
{
	return g_KeyState[InputKeyIndex(InKey)];
}

FORCEINLINE void SetKeyDownState(EInputKey InKey, bool InDown)
{
	if (InKey != EInputKey::UNKNOWN)
		g_KeyState[InputKeyIndex(InKey)] = InDown;
}

FORCEINLINE void ResetAllKeyState()
{
	g_KeyState.fill(false);
}

FORCEINLINE EInputKey ToInputKeyFromWin32(int32 InVK)
{
	switch (InVK)
	{
	case 'A': return EInputKey::A;
	case 'B': return EInputKey::B;
	case 'C': return EInputKey::C;
	case 'D': return EInputKey::D;
	case 'E': return EInputKey::E;
	case 'F': return EInputKey::F;
	case 'G': return EInputKey::G;
	case 'H': return EInputKey::H;
	case 'I': return EInputKey::I;
	case 'J': return EInputKey::J;
	case 'K': return EInputKey::K;
	case 'L': return EInputKey::L;
	case 'M': return EInputKey::M;
	case 'N': return EInputKey::N;
	case 'O': return EInputKey::O;
	case 'P': return EInputKey::P;
	case 'Q': return EInputKey::Q;
	case 'R': return EInputKey::R;
	case 'S': return EInputKey::S;
	case 'T': return EInputKey::T;
	case 'U': return EInputKey::U;
	case 'V': return EInputKey::V;
	case 'W': return EInputKey::W;
	case 'X': return EInputKey::X;
	case 'Y': return EInputKey::Y;
	case 'Z': return EInputKey::Z;
	case '0': return EInputKey::NUM0;
	case '1': return EInputKey::NUM1;
	case '2': return EInputKey::NUM2;
	case '3': return EInputKey::NUM3;
	case '4': return EInputKey::NUM4;
	case '5': return EInputKey::NUM5;
	case '6': return EInputKey::NUM6;
	case '7': return EInputKey::NUM7;
	case '8': return EInputKey::NUM8;
	case '9': return EInputKey::NUM9;
	case VK_OEM_PLUS:
	case VK_ADD: return EInputKey::PLUS;
	case VK_OEM_MINUS:
	case VK_SUBTRACT: return EInputKey::MINUS;
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT: return EInputKey::SHIFT;
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL: return EInputKey::CTRL;
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU: return EInputKey::ALT;
	case VK_DELETE: return EInputKey::DELETE_KEY;
	case VK_ESCAPE: return EInputKey::ESCAPE;
	case VK_SPACE: return EInputKey::SPACE;
	case VK_RETURN: return EInputKey::ENTER;
	case VK_TAB: return EInputKey::TAB;
	case VK_LEFT: return EInputKey::LEFT;
	case VK_RIGHT: return EInputKey::RIGHT;
	case VK_UP: return EInputKey::UP;
	case VK_DOWN: return EInputKey::DOWN;
	default: return EInputKey::UNKNOWN;
	}
}

FORCEINLINE EInputKey ToInputKeyFromGLFW(int32 InGlfwKey)
{
	switch (InGlfwKey)
	{
	case GLFW_KEY_A: return EInputKey::A;
	case GLFW_KEY_B: return EInputKey::B;
	case GLFW_KEY_C: return EInputKey::C;
	case GLFW_KEY_D: return EInputKey::D;
	case GLFW_KEY_E: return EInputKey::E;
	case GLFW_KEY_F: return EInputKey::F;
	case GLFW_KEY_G: return EInputKey::G;
	case GLFW_KEY_H: return EInputKey::H;
	case GLFW_KEY_I: return EInputKey::I;
	case GLFW_KEY_J: return EInputKey::J;
	case GLFW_KEY_K: return EInputKey::K;
	case GLFW_KEY_L: return EInputKey::L;
	case GLFW_KEY_M: return EInputKey::M;
	case GLFW_KEY_N: return EInputKey::N;
	case GLFW_KEY_O: return EInputKey::O;
	case GLFW_KEY_P: return EInputKey::P;
	case GLFW_KEY_Q: return EInputKey::Q;
	case GLFW_KEY_R: return EInputKey::R;
	case GLFW_KEY_S: return EInputKey::S;
	case GLFW_KEY_T: return EInputKey::T;
	case GLFW_KEY_U: return EInputKey::U;
	case GLFW_KEY_V: return EInputKey::V;
	case GLFW_KEY_W: return EInputKey::W;
	case GLFW_KEY_X: return EInputKey::X;
	case GLFW_KEY_Y: return EInputKey::Y;
	case GLFW_KEY_Z: return EInputKey::Z;
	case GLFW_KEY_0: return EInputKey::NUM0;
	case GLFW_KEY_1: return EInputKey::NUM1;
	case GLFW_KEY_2: return EInputKey::NUM2;
	case GLFW_KEY_3: return EInputKey::NUM3;
	case GLFW_KEY_4: return EInputKey::NUM4;
	case GLFW_KEY_5: return EInputKey::NUM5;
	case GLFW_KEY_6: return EInputKey::NUM6;
	case GLFW_KEY_7: return EInputKey::NUM7;
	case GLFW_KEY_8: return EInputKey::NUM8;
	case GLFW_KEY_9: return EInputKey::NUM9;
	case GLFW_KEY_EQUAL:
	case GLFW_KEY_KP_ADD: return EInputKey::PLUS;
	case GLFW_KEY_MINUS:
	case GLFW_KEY_KP_SUBTRACT: return EInputKey::MINUS;
	case GLFW_KEY_LEFT_SHIFT:
	case GLFW_KEY_RIGHT_SHIFT: return EInputKey::SHIFT;
	case GLFW_KEY_LEFT_CONTROL:
	case GLFW_KEY_RIGHT_CONTROL: return EInputKey::CTRL;
	case GLFW_KEY_LEFT_ALT:
	case GLFW_KEY_RIGHT_ALT: return EInputKey::ALT;
	case GLFW_KEY_DELETE: return EInputKey::DELETE_KEY;
	case GLFW_KEY_ESCAPE: return EInputKey::ESCAPE;
	case GLFW_KEY_SPACE: return EInputKey::SPACE;
	case GLFW_KEY_ENTER:
	case GLFW_KEY_KP_ENTER: return EInputKey::ENTER;
	case GLFW_KEY_TAB: return EInputKey::TAB;
	case GLFW_KEY_LEFT: return EInputKey::LEFT;
	case GLFW_KEY_RIGHT: return EInputKey::RIGHT;
	case GLFW_KEY_UP: return EInputKey::UP;
	case GLFW_KEY_DOWN: return EInputKey::DOWN;
	default: return EInputKey::UNKNOWN;
	}
}

FORCEINLINE uint64 GetInputTimeMS()
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return (uint64)std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

FORCEINLINE void EnsureMouseButtonsInitialized()
{
}

FORCEINLINE void ResetMouseClickedState()
{
	EnsureMouseButtonsInitialized();
	for (auto& iter : g_MouseState)
	{
		iter.ResetFrameEvents();
	}
}

FORCEINLINE void ResetMouseAllState()
{
	EnsureMouseButtonsInitialized();
	for (auto& iter : g_MouseState)
	{
		iter.ResetAll();
	}
}

FORCEINLINE void UpdateMousePosition(int32 InMouseX, int32 InMouseY)
{
	EnsureMouseButtonsInitialized();
	g_MousePosX = InMouseX;
	g_MousePosY = InMouseY;
	for (auto& iter : g_MouseState)
	{
		iter.UpdateDragState(InMouseX, InMouseY);
	}
}

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
#define USE_RAYTRACING 1
#define USE_RESOURCE_BARRIER_BATCHER 1
#define USE_PATH_TRACING 1

extern bool GUseRealTimeShaderUpdate;
extern bool GSupportRaytracing;
extern bool GSupportInlineRaytracing;
extern int32 GMaxCheckCountForRealTimeShaderUpdate;
extern int32 GSleepMSForRealTimeShaderUpdate;

extern std::thread::id GMainThreadID;
extern bool IsMainThread();
extern bool IsMainThread(const std::thread::id& InThreadId);

extern bool GRHISupportVsync;
extern bool GUseVsync;

#endif //PCH_H
