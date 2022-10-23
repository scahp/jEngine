#include "pch.h"

std::map<int, bool> g_KeyState;
std::map<EMouseButtonType, bool> g_MouseState;
float g_timeDeltaSecond = 0.0f;

#if USE_VULKAN
#pragma comment(lib, "vulkan-1.lib")
#endif

int32 SCR_WIDTH = 1280;
int32 SCR_HEIGHT = 720;
bool IsSizeMinimize = false;

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dxguid.lib")

uint32 GetMaxThreadCount()
{
    static uint32 MaxThreadCount = Max((uint32)1, std::thread::hardware_concurrency());
    return MaxThreadCount;
}
