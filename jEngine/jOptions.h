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
    bool ShowDebugObject = false;
    bool BloomEyeAdaptation = false;
    bool QueueSubmitAfterShadowPass = false;
    bool QueueSubmitAfterBasePass = false;
    Vector CameraPos = Vector::ZeroVector;
    float AutoExposureKeyValueScale = 1.0f;
};

extern jOptions gOptions;