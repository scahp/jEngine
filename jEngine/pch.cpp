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

// DirectX 12 Agility SDK 613
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 613; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

std::wstring ConvertToWchar(const char* InPath, int32 InLength)
{
    check(InPath);

    std::wstring result;
    if (InLength > 0)
    {
        result.resize(InLength + 1);
        {
            const int32 ResultFilePathLength = MultiByteToWideChar(CP_ACP, 0, InPath, -1, NULL, NULL);
            check(ResultFilePathLength < 256);

            MultiByteToWideChar(CP_ACP, 0, InPath
                , InLength, &result[0], (int32)(result.size() - 1));
            result[InLength] = 0;
        }
    }
    return result;
}

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
bool GSupportRaytracing = false;
int32 GMaxCheckCountForRealTimeShaderUpdate = 10;
int32 GSleepMSForRealTimeShaderUpdate = 100;
std::thread::id GMainThreadID;

bool IsMainThread()
{
    return GMainThreadID == std::this_thread::get_id();
}

bool IsMainThread(const std::thread::id& InThreadId)
{
    return GMainThreadID == InThreadId;
}

extern bool GRHISupportVsync = false;
extern bool GUseVsync = false;

#ifdef _DEBUG
#pragma comment(lib, "Debug/DirectXTex.lib")
#else
#pragma comment(lib, "Release/DirectXTex.lib")
#endif
