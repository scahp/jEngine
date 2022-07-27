#include "pch.h"
#include "jRHI.h"

std::map<int, bool> g_KeyState;
std::map<EMouseButtonType, bool> g_MouseState;
float g_timeDeltaSecond = 0.0f;
const uint32 MaxQueryTimeCount = 512;

#if USE_VULKAN
#pragma comment(lib, "vulkan-1.lib")
#endif
