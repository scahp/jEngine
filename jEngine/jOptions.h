#pragma once
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
    bool UseSSGI;
    bool UseSSGITemporalAccumulation;
    float SSGIAccumBlendFactor;
    float SSGIIntensity;

    // SSGI Reprojection
    bool UseSSGIReprojection;
    bool UseDiscontinuityWeightForSSGI;

    // SSGI Denoising
    bool UseSSGIDenoising;
    const char* SSGIDenoiser;
    int32 SSGIDenoiserKernelSize;
    float SSGIDenoiserKernelSigma;
    float SSGIDenoiserBilateralKernelSigma;
    int32 SSGI_BlurQuality;

    // AO
    const char* Denoiser;
    int32 AOType;
    const char* UseResolution;
    bool ShowDebugRT;
    bool ShowAOOnly;
    bool UseAOReprojection;
    bool UseDiscontinuityWeight;
    bool UseHaltonJitter;
    bool UseAccumulateRay;
    int32 GaussianKernelSize;
    float GaussianKernelSigma;
    float BilateralKernelSigma;
    int32 RayPerPixel;

    // Path Tracing
    int32 MaxRecursionDepthForPathTracing;
    int32 RayPerPixelForPathTracing;

    // Raytracing
    bool UseRaytracing;

    bool IsDenoiserGuassian() const { return GDenoisers[0] == Denoiser; }
    bool IsDenoiserGuassianSeparable() const { return GDenoisers[1] == Denoiser; }
    bool IsDenoiserBilateral() const { return GDenoisers[2] == Denoiser; }

    int32 GetRTAOIndex() const { return 1; }
    bool IsRTAO() const { return AOType == GetRTAOIndex(); }
    bool IsSSAO() const { return AOType == 2; }
};

extern jOptions gOptions;

extern std::vector<std::string> gPathTracingScenes;
extern std::vector<std::string> gPathTracingScenesNameOnly;
extern const char* gSelectedScene;
extern int32 gSelectedSceneIndex;
