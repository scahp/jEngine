#pragma once
#include "Math/Vector.h"

struct jOptions
{
    bool UseVRS = false;
    bool ShowVRSArea = false;
    bool ShowGrid = false;
    bool UseWaveIntrinsics = false;
    bool UseDeferredRenderer = true;
    bool UseSubpass = false;
    bool UseMemoryless = true;
    bool ShowDebugObject = false;
    bool BloomEyeAdaptation = false;
    bool QueueSubmitAfterShadowPass = true;
    bool QueueSubmitAfterBasePass = true;
    Vector CameraPos = Vector::ZeroVector;
    float AutoExposureKeyValueScale = 1.0f;
    float Metallic = 0.0f;
    float Roughness = 0.2f;
};

extern jOptions gOptions;