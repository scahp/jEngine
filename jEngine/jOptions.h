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
    bool BloomEyeAdaptation = true;
    bool QueueSubmitAfterShadowPass = true;
    bool QueueSubmitAfterBasePass = true;
    Vector CameraPos = Vector::ZeroVector;
    float AutoExposureKeyValueScale = 0.1f;
    float Metallic = 0.0f;
    float Roughness = 0.2f;
    Vector SunDir = Vector(0.31f, -0.828f, -0.241f);
    float AnisoG = 0.15f;
    bool EarthQuake = false;
    float FocalDistance = 5.0f;
    float LensRadius = 0.2f;
};

extern jOptions gOptions;