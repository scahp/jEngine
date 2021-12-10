#include "pch.h"
#include "jRHI.h"

static uint32 sScreenWidth = SCR_WIDTH;
static uint32 sScreenHeight = SCR_HEIGHT;
static bool sIsSizeMinimize = false;

uint32 GetScreenWidth()
{
    return sScreenWidth;
}

uint32 GetScreenHeight()
{
    return sScreenHeight;
}

bool GetIsSizeMinimize()
{
    return sIsSizeMinimize;
}

void SetScreenWidth(uint32 InWidth)
{
    sScreenWidth = InWidth;
}

void SetScreenHeight(uint32 InHeight)
{
    sScreenHeight = InHeight;
}

void SetIsSizeMinimize(bool InIsSizeMinimize)
{
    sIsSizeMinimize = InIsSizeMinimize;
}

float GetScreenAspect()
{
    return static_cast<float>(GetScreenWidth()) / static_cast<float>(GetScreenHeight());
}

std::map<int, bool> g_KeyState;
std::map<EMouseButtonType, bool> g_MouseState;
float g_timeDeltaSecond = 0.0f;
