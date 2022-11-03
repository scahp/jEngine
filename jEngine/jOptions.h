#pragma once
#include "Math/Vector.h"

struct jOptions
{
    bool UseVRS = false;
    bool ShowVRSArea = false;
    bool ShowGrid = false;
    bool UseWaveIntrinsics = false;
    Vector CameraPos = Vector::ZeroVector;
};

extern jOptions gOptions;