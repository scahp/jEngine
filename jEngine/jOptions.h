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

extern const char* GDenoisers[(int32)EDenoiser::MAX];
extern const char* GAOResolution[3];
extern const char* GWaitPrerequsiteGraphicsQueueTask[4];
extern const char* GAOType[3];
extern const char* GHWRTDebugViewModes[14];
extern const char* GHWRTDirectLightingModes[2];

#ifndef SURFEL_GI_CASCADE_COUNT
#define SURFEL_GI_CASCADE_COUNT 3
#endif
#define SURFEL_GI_CASCADE_PACKED_COUNT ((SURFEL_GI_CASCADE_COUNT + 3) / 4)

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
    bool UseHWRTDirectLighting;
    int32 HWRTDirectLightingMode;
    int32 HWRTDebugViewMode;
    float HWRTDebugLineWidth;
    float HWRTDebugUVScale;
    float HWRTDebugPrimitiveIDScale;
    bool HWRTForceMipLevel0;
    float HWRTNormalBias;
    float HWRTShadowRayStartOffset;
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
    Vector DefaultSunDir;
    Vector DirectionalLightColor;
    float DirectionalLightIntensity;
    float AnisoG;
    bool EarthQuake;
    float FocalDistance;
    float LensRadius;
    float AORadius;
    float SSAOBias;
    float AOIntensity;
    bool UseSSGI;
    bool UseSurfelGI;
    bool UseSSGITemporalAccumulation;
    bool ShowSSGIOnly;
    bool UseSSGIAttenuation;
    float SSGIAccumBlendFactor;
    float SSGIIntensity;
    int32 SSGIMaxSteps;
    float SSGIMaxDistance;
    int32 SSGIRayCount;
    float SSGIResolutionScale;
    int32 SurfelGIMaxSurfels;
    int32 SurfelGISpawnBudgetPerFrame;
    int32 SurfelGITileSize;
    float SurfelGIMergeDistanceScale;
    float SurfelGIRadiusScale;
    // Candidate face margin = candidate radius * SurfelGIFaceMarginRadiusScale.
    // Used to reject candidates too close to multiple cell faces (edge/corner crowding).
    float SurfelGIFaceMarginRadiusScale;
    float SurfelGINormalThreshold;
    int32 SurfelGITTLInFrames;
    float SurfelGIWorldGridCellSize;
    float SurfelGICascadeCellScaleFromPrev[SURFEL_GI_CASCADE_COUNT];
    float SurfelGICascadeStartDistance[SURFEL_GI_CASCADE_COUNT];
    float SurfelGICascadeRadiusScale[SURFEL_GI_CASCADE_COUNT];
    int32 SurfelGIClipmapGridDimX[SURFEL_GI_CASCADE_COUNT];
    int32 SurfelGIClipmapGridDimY[SURFEL_GI_CASCADE_COUNT];
    int32 SurfelGIClipmapGridDimZ[SURFEL_GI_CASCADE_COUNT];
    int32 SurfelGISurfelsPerCell[SURFEL_GI_CASCADE_COUNT];
    bool UseSurfelGICenterSpawnBias;
    float SurfelGINearKeepRadius;
    float SurfelGINearSpawnBias;
    float SurfelGIFrustumInteriorScale;
    float SurfelGIFarNearFactorThreshold;
    float SurfelGIFarMaxDistanceMultiplier;
    float SurfelGIReplaceNearDelta;
    float SurfelGIStaleAgeDivisor;
    bool SurfelGIReservoirEnable;
    int32 SurfelGIReservoirPerCellLimit;
    float SurfelGIReservoirTableCapacityScale;
    int32 SurfelGISpawnHysteresisFrames;
    int32 SurfelGIDeleteHysteresisFrames;
    bool ShowSurfelGIDebug;
    bool ShowSurfelGIPlacedSurfels;
    bool ShowSurfelGIStateDebug;
    bool ShowSurfelGICellDebug;
    bool ShowSurfelGIUnderfilledCellDebug;
    bool ShowSurfelGICellGrid;
    bool ShowSurfelGISpawnAttemptDebug;
    bool ShowSurfelGIIrradianceDebug;
    bool SurfelGIInlineRayEnable;
    int32 SurfelGIInlineRayCount;
    float SurfelGIInlineRayMaxDistance;
    float SurfelGIInlineRayNormalBias;
    float SurfelGIInlineRayHistoryBlend;
    int32 SurfelGIVisualizeNeighborCellRadius;
    bool SurfelGIVisualizeBlendWithScene;
    float SurfelGIVisualizeBlendAlpha;

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

    float LightColorScale = 1.0f;

    FORCEINLINE bool IsDenoiserGuassian() const { return Denoiser == EDenoiser::GAUSSIAN; }
    FORCEINLINE bool IsDenoiserGuassianSeparable() const { return Denoiser == EDenoiser::GAUSSIAN_SEPARABLE; }
    FORCEINLINE bool IsDenoiserBilateral() const { return Denoiser == EDenoiser::BILATERAL; }

    FORCEINLINE bool IsSSGIDenoiserGuassian() const { return SSGIDenoiser == EDenoiser::GAUSSIAN; }
    FORCEINLINE bool IsSSGIDenoiserGuassianSeparable() const { return SSGIDenoiser == EDenoiser::GAUSSIAN_SEPARABLE; }
    FORCEINLINE bool IsSSGIDenoiserBilateral() const { return SSGIDenoiser == EDenoiser::BILATERAL; }
    FORCEINLINE bool IsSSGIDenoiserBilateralPS() const { return SSGIDenoiser == EDenoiser::BILATERAL_PS; }
    FORCEINLINE bool IsSSGIDenoise_A_Trous() const { return SSGIDenoiser == EDenoiser::A_TROUS; }
    FORCEINLINE bool HasAnyReprojection() const { return UseAOReprojection || UseSSGIReprojection; }

    const char* GetDenoiseName(EDenoiser InDenoiser) const;

    FORCEINLINE int32 GetRTAOIndex() const { return 1; }
    FORCEINLINE bool IsRTAO() const { return AOType == GetRTAOIndex(); }
    FORCEINLINE bool IsSSAO() const { return AOType == 2; }
};

extern jOptions gOptions;

extern std::vector<std::string> gPathTracingScenes;
extern std::vector<std::string> gPathTracingScenesNameOnly;
extern const char* gSelectedScene;
extern int32 gSelectedSceneIndex;
