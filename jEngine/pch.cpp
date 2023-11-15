#include "pch.h"

std::map<int, bool> g_KeyState;
std::map<EMouseButtonType, bool> g_MouseState;
float g_timeDeltaSecond = 0.0f;

#pragma comment(lib, "vulkan-1.lib")

EAPIType gAPIType = EAPIType::None;

extern bool IsUseVulkan()
{
    return (gAPIType == EAPIType::Vulkan);
}

extern bool IsUseDX12()
{
    return (gAPIType == EAPIType::DX12);
}

//#endif

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

#if USE_PIX
#pragma comment(lib, "WinPixEventRuntime.lib")
#endif

jEngine* g_Engine = nullptr;

bool GUseRealTimeShaderUpdate = true;
int32 GMaxCheckCountForRealTimeShaderUpdate = 10;
int32 GSleepMSForRealTimeShaderUpdate = 100;
