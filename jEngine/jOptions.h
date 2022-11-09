#pragma once
#include "Math/Vector.h"

struct jOptions
{
    bool UseVRS = false;
    bool ShowVRSArea = false;
    bool ShowGrid = false;
    bool UseWaveIntrinsics = false;
    bool UseDeferredRenderer = true;
    bool UseSubpass = true;
    bool UseMemoryless = true;
    Vector CameraPos = Vector::ZeroVector;
};

extern jOptions gOptions;