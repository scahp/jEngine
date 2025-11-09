#pragma once
#include "Math/Vector.h"

enum class EDenoiser : int32
{
    NONE = 0,
    GAUSSIAN,
    GAUSSIAN_SEPARABLE,
    BILATERAL,
    BILATERAL_PS,
    A_TROUS,
    MAX
};

enum class EGIType : int32
{
    SSGI = 0,
    IRRADIANCE_PROBES,
    MAX
};

extern const char* GDenoisers[(int32)EDenoiser::MAX];
extern const char* GGITypes[(int32)EGIType::MAX];
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
    Vector DirectionalLightColor;
    float DirectionalLightIntensity;
    float AnisoG;
    bool EarthQuake;
    float FocalDistance;
    float LensRadius;
    float AORadius;
    float SSAOBias;
    float AOIntensity;
    EGIType GIType;
    bool UseSSGI;
    bool UseSSGITemporalAccumulation;
    bool ShowSSGIOnly;
    bool UseSSGIAttenuation;
    float SSGIAccumBlendFactor;
    float SSGIIntensity;
    int32 SSGIMaxSteps;
    float SSGIMaxDistance;
    int32 SSGIRayCount;
    float SSGIResolutionScale;

    // SSGI Reprojection
    bool UseSSGIReprojection;
    bool UseDiscontinuityWeightForSSGI;

    // SSGI Denoising
    EDenoiser SSGIDenoiser;
    int32 SSGIDenoiserKernelSize;
    float SSGIDenoiserKernelSigma;
    float SSGIDenoiserBilateralKernelSigma;
    int32 SSGIBlurQuality;

    // SSGI A-Trous Denoising
    float SSGIATrousSigmaColor;
    float SSGIATrousSigmaNormal;
    float SSGIATrousSigmaDepth;

    // AO
    EDenoiser Denoiser;
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

    bool IsDenoiserGuassian() const { return Denoiser == EDenoiser::GAUSSIAN; }
    bool IsDenoiserGuassianSeparable() const { return Denoiser == EDenoiser::GAUSSIAN_SEPARABLE; }
    bool IsDenoiserBilateral() const { return Denoiser == EDenoiser::BILATERAL; }

    bool IsSSGIDenoiserGuassian() const { return SSGIDenoiser == EDenoiser::GAUSSIAN; }
    bool IsSSGIDenoiserGuassianSeparable() const { return SSGIDenoiser == EDenoiser::GAUSSIAN_SEPARABLE; }
    bool IsSSGIDenoiserBilateral() const { return SSGIDenoiser == EDenoiser::BILATERAL; }
    bool IsSSGIDenoiserBilateralPS() const { return SSGIDenoiser == EDenoiser::BILATERAL_PS; }
    bool IsSSGIDenoise_A_Trous() const { return SSGIDenoiser == EDenoiser::A_TROUS; }

    const char* GetDenoiseName(EDenoiser InDenoiser) const;

    int32 GetRTAOIndex() const { return 1; }
    bool IsRTAO() const { return AOType == GetRTAOIndex(); }
    bool IsSSAO() const { return AOType == 2; }
};

extern jOptions gOptions;

extern std::vector<std::string> gPathTracingScenes;
extern std::vector<std::string> gPathTracingScenesNameOnly;
extern const char* gSelectedScene;
extern int32 gSelectedSceneIndex;
