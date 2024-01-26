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
    float AutoExposureKeyValueScale = -0.2f;
    float Metallic = 0.0f;
    float Roughness = 0.2f;
    Vector SunDir = Vector(0.31f, -0.828f, -0.241f);
    float AnisoG = 0.15f;
    bool EarthQuake = false;
    float FocalDistance = 5.0f;
    float LensRadius = 0.05f;
    float AORadius = 20.0f;
    float AOIntensity = 1.0f;
    int32 SamplePerPixel = 1;
    bool UseRTAO = true;
    bool UseAOReprojection = true;
    bool ShowDebugRT = true;
};

extern jOptions gOptions;