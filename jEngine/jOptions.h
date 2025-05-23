﻿#pragma once
#include "Math/Vector.h"

extern const char* GDenoisers[4];
extern const char* GAOResolution[3];
extern const char* GWaitPrerequsiteGraphicsQueueTask[4];
extern const char* GAOType[3];

struct jOptions
{
    jOptions();
    
    bool operator==(struct jOptions const& RHS) const;

    // RHI options
    const bool EnableDebuggerLayer;

    // Graphics options
    bool UseVRS;
    bool ShowVRSArea;
    bool ShowGrid;
    bool UseWaveIntrinsics;
    bool UseDeferredRenderer;
    bool UseSubpass;
    bool UseMemoryless;
    bool ShowDebugObject;
    bool BloomEyeAdaptation;
    bool QueueSubmitAfterShadowPass;
    bool QueueSubmitAfterBasePass;
    Vector CameraPos;
    float AutoExposureKeyValueScale;
    float Metallic;
    float Roughness;
    Vector SunDir;
    float AnisoG;
    bool EarthQuake;
    float FocalDistance;
    float LensRadius;
    float AORadius;
    float SSAOBias;
    float AOIntensity;
    int32 RayPerPixel;
    int32 AOType;
    bool UseAOReprojection;
    const char* Denoiser;
    int32 GaussianKernelSize;
    float GaussianKernelSigma;
    float BilateralKernelSigma;
    bool ShowAOOnly;
    bool ShowDebugRT;
    bool UseAccumulateRay;
    bool UseDiscontinuityWeight;
    bool UseHaltonJitter;
    const char* UseResolution;
    
    int32 MaxRecursionDepthForPathTracing;
    int32 RayPerPixelForPathTracing;

    bool IsDenoiserGuassian() const { return GDenoisers[0] == Denoiser; }
    bool IsDenoiserGuassianSeparable() const { return GDenoisers[1] == Denoiser; }
    bool IsDenoiserBilateral() const { return GDenoisers[2] == Denoiser; }

    int32 GetRTAOIndex() const { return 1; }
    bool IsRTAO() const;
    bool IsSSAO() const;
};

extern jOptions gOptions;

extern std::vector<std::string> gPathTracingScenes;
extern std::vector<std::string> gPathTracingScenesNameOnly;
extern const char* gSelectedScene;
extern int32 gSelectedSceneIndex;
