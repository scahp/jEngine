#pragma once
#include "Math/Vector.h"

struct jOptions
{
    bool UseVRS = false;
    bool ShowVRSArea = false;
    bool ShowGrid = false;
    bool UseWaveIntrinsics = false;
    bool UseDeferredRenderer = false;
    Vector CameraPos = Vector::ZeroVector;
};

extern jOptions gOptions;