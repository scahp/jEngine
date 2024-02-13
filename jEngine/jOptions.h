#pragma once
#include "Math/Vector.h"

extern const char* GDenoisers[4];
extern const char* GAOResolution[3];
extern const char* GWaitPrerequsiteGraphicsQueueTask[4];

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
    float AOIntensity;
    int32 RayPerPixel;
    bool UseRTAO;
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

    // Begin AsyncComputeOption
    bool UseAsyncComputeQueue;
    const char* WaitPrerequsiteGraphicsQueueTask;
    bool WaitSubsequentGraphicsQueueTask;
    bool IsPrerequsiteNone() const { return GWaitPrerequsiteGraphicsQueueTask[0] == WaitPrerequsiteGraphicsQueueTask; }
    bool IsPrerequsiteShadowPass() const { return GWaitPrerequsiteGraphicsQueueTask[1] == WaitPrerequsiteGraphicsQueueTask; }
    bool IsPrerequsiteBasePass() const { return GWaitPrerequsiteGraphicsQueueTask[2] == WaitPrerequsiteGraphicsQueueTask; }
    bool IsPrerequsiteShadowAtmosphericPass() const { return GWaitPrerequsiteGraphicsQueueTask[3] == WaitPrerequsiteGraphicsQueueTask; }
    // End AsyncComputeOption

    bool IsDenoiserGuassian() const { return GDenoisers[0] == Denoiser; }
    bool IsDenoiserGuassianSeparable() const { return GDenoisers[1] == Denoiser; }
    bool IsDenoiserBilateral() const { return GDenoisers[2] == Denoiser; }
};

extern jOptions gOptions;