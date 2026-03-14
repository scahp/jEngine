#include "pch.h"
#include "jOptions.h"

std::vector<std::string> gPathTracingScenes;
std::vector<std::string> gPathTracingScenesNameOnly;
const char* gSelectedScene = nullptr;
int32 gSelectedSceneIndex = 0;

const char* GDenoisers[(int32)EDenoiser::MAX] = { "None", "Gaussian", "GaussianSeparable", "Bilateral", "BilateralPS", "A-Trous" };
const char* GAOResolution[3] = { "100", "75", "50" };
const char* GAtmosphereResolution[3] = { "100", "75", "50" };
extern const char* GAOType[3] = { "NoAO", "RTAO", "SSAO" };
const char* GHWRTDirectLightingModes[2] = { "DispatchRays", "Inline RayQuery" };
const char* GHWRTDebugViewModes[14] = {
    "Shaded",
    "Triangle Edge",
    "UV",
    "UV Grid",
    "Primitive ID",
    "Barycentric",
    "Texel Density",
    "World Normal",
    "Geometric Normal",
    "Albedo Texture",
    "Normal Texture",
    "RM Texture",
    "Mip Level",
    "Opaque/NonOpaque"
};

static_assert(_countof(GDenoisers) == (int32)EDenoiser::MAX, "EDenoiser count mismatch");
static_assert(_countof(GHWRTDirectLightingModes) == 2, "GHWRTDirectLightingModes count mismatch");
static_assert(_countof(GHWRTDebugViewModes) == 14, "GHWRTDebugViewModes count mismatch");

jOptions gOptions;

jOptions::jOptions()
	// RHI options
	: EnableDebuggerLayer(true)
{

	// Graphics options
	UseVRS = false;
	ShowVRSArea = false;
	ShowGrid = false;
	UseWaveIntrinsics = false;
	UseDeferredRenderer = true;
	UseHWRTDirectLighting = false;
    HWRTDirectLightingMode = 0;
    HWRTDebugViewMode = 0;
    HWRTDebugLineWidth = 0.02f;
    HWRTDebugUVScale = 16.0f;
    HWRTDebugPrimitiveIDScale = 1.0f;
    HWRTForceMipLevel0 = false;
    HWRTNormalBias = 1.0f;
    HWRTShadowRayStartOffset = 0.001f;
	UseSubpass = false;
	UseMemoryless = true;
	ShowDebugObject = false;
	BloomEyeAdaptation = false;
	QueueSubmitAfterShadowPass = true;
	QueueSubmitAfterBasePass = true;
	CameraPos = Vector::ZeroVector;
	AutoExposureKeyValueScale = -0.2f;
	Metallic = 0.0f;
	Roughness = 0.2f;
	//SunDir = Vector(0.31f, -0.828f, -0.241f);
	DefaultSunDir = Vector(0.049f, -0.953f, -0.263f);
	DirectionalLightColor = Vector(0.074f, 0.059f, 0.028f);
	DirectionalLightIntensity = 30.0f;
    AnisoG = 0.15f;
    UseAtmosphericShadowing = false;
    AtmosphereResolution = GAtmosphereResolution[2];
    AtmosphericShadowSlopeOfDist = 0.25f;
    AtmosphericShadowInScatteringLambda = 0.001f;
    AtmosphericShadowApplyIntensity = 0.1f;
    AtmosphericShadowTravelCount = 256;
    AtmosphericShadowUseNoise = true;
	EarthQuake = false;
	FocalDistance = 5.0f;
	LensRadius = 0.05f;
	AORadius = 50.0f;
	SSAOBias = AORadius / 20.0f;
	AOIntensity = 1.0f;
	UseSSGI = false;
    UseSurfelGI = true;
	UseSSGITemporalAccumulation = true;
	ShowSSGIOnly = false;
	UseSSGIAttenuation = false;
	SSGIAccumBlendFactor = 0.98f;  // Higher blend factor for more temporal smoothing
    SSGIIntensity = 25.0f;
    SSGIMaxSteps = 4;
    SSGIMaxDistance = 50.0f;
    SSGIRayCount = 4;
    SSGIResolutionScale = 0.5f;
    SurfelGIMaxSurfels = 131072;
    SurfelGISpawnBudgetPerFrame = 2048;
    UseSurfelGITileBasedSampling = true;
    SurfelGITileSize = 8;
    SurfelGIRadiusScale = 0.5f;
    SurfelGIFaceMarginRadiusScale = 0.5f;
    SurfelGINormalThreshold = 0.8f;
    SurfelGITTLInFrames = 120;
    SurfelGIWorldGridCellSize = 30.0f;
    const int32 ReservoirPerCellDefault = 5;
    SurfelGIReservoirPerCellLimit = ReservoirPerCellDefault;
    for (int32 i = 0; i < SURFEL_GI_CASCADE_COUNT; ++i)
    {
        SurfelGICascadeCellScaleFromPrev[i] = (float)(i + 1);
        SurfelGICascadeStartDistance[i] = (i == 0) ? 0.0f : (float)(i * 600.0f);
        SurfelGICascadeRadiusScale[i] = i + 1;
        SurfelGIClipmapGridDimX[i] = (i == 0) ? 64 : 48;
        SurfelGIClipmapGridDimY[i] = (i == 0) ? 64 : 48;
        SurfelGIClipmapGridDimZ[i] = (i == 0) ? 32 : 24;
        SurfelGISurfelsPerCell[i] = ReservoirPerCellDefault;
    }
    UseSurfelGIPreferCellCenterForFirstPlacement = true;
    SurfelGIOutOfViewKeepFrames = 120;
    ShowSurfelGIDebug = false;
    ShowSurfelGIPlacedSurfels = false;
    ShowSurfelGIStateDebug = false;
    ShowSurfelGICellDebug = false;
    ShowSurfelGIUnderfilledCellDebug = false;
    ShowSurfelGICellGrid = false;
    ShowSurfelGISpawnAttemptDebug = false;
    ShowSurfelGIIrradianceDebug = false;
    ShowSurfelGIHoverRayDebug = false;
    ShowSurfelGIHoverRayHitRadianceColor = false;
    SurfelGIInlineRayEnable = true;
    SurfelGIInlineRayGuideEnable = true;
    SurfelGIInlineRayCount = 16;
    SurfelGINewSurfelBootstrapRayCount = 32;
    SurfelGIRadianceScale = 1.5f;
    SurfelGIInlineRayMaxDistance = 5000.0f;
    SurfelGIInlineRayNormalBias = 1.0f;
    SurfelGIInlineRayHistoryBlend = 0.9f;
    SurfelGIIntensity = 1.0f;
    SurfelGIIrradianceDebugMode = 0;
    SurfelGIVisualizeNeighborCellRadius = 1;
    SurfelGIResolveSoftness = 2.957f;
    SurfelGIResolveIrradianceWarmupUpdates = 16.0f;
    SurfelGIVisualizeBlendWithScene = true;
    SurfelGIVisualizeBlendAlpha = 1.0f;

    // SSGI Reprojection
    UseSSGIReprojection = true;
    UseDiscontinuityWeightForSSGI = true;

    // SSGI Denoising
    SSGIDenoiser = EDenoiser::A_TROUS;
    SSGIDenoiserKernelSize = 9;
    SSGIDenoiserKernelSigma = 2.5f;
    SSGIDenoiserBilateralKernelSigma = 0.01f;
    SSGIBlurQuality = 3;  // Reduced from 5 to balance noise vs edge preservation

    // SSGI A-Trous Denoising
    SSGIATrousSigmaColor = 2.0f;
    SSGIATrousSigmaNormal = 0.5f;
    SSGIATrousSigmaDepth = 2.5f;

    // AO
    Denoiser = EDenoiser::GAUSSIAN;
    AOType = GetRTAOIndex();
    UseResolution = GAOResolution[0];
    ShowDebugRT = false;
    ShowAOOnly = false;
    UseAOReprojection = true;
    UseDiscontinuityWeight = true;
    UseHaltonJitter = true;
    UseAccumulateRay = true;
    GaussianKernelSize = 9;
    GaussianKernelSigma = 2.5f;
    BilateralKernelSigma = 0.01f;
    RayPerPixel = 1;

    // Path Tracing
    MaxRecursionDepthForPathTracing = 1;
    RayPerPixelForPathTracing = 1;

	// Raytracing
	UseRaytracing = true;

#if ENABLE_PBR
    // PBR will use light color as a flux,
    LightColorScale = 20000.0f;
#endif
}


const char* jOptions::GetDenoiseName(EDenoiser InDenoiser) const
{
    for (int32 i = 0; i < (int32)EDenoiser::MAX; ++i)
    {
        if ((int32)InDenoiser == i)
            return GDenoisers[i];
    }
    return GDenoisers[0];
}

bool jOptions::operator==(struct jOptions const& RHS) const
{
	static_assert(std::is_trivially_copyable<jOptions>::value, "jOptions should be trivially copyable!");

	return 0 == memcmp(this, &RHS, sizeof(jOptions));
}
