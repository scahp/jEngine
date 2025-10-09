#include "pch.h"
#include "jOptions.h"

std::vector<std::string> gPathTracingScenes;
std::vector<std::string> gPathTracingScenesNameOnly;
const char* gSelectedScene = nullptr;
int32 gSelectedSceneIndex = 0;

const char* GDenoisers[(int32)EDenoiser::MAX] = { "None", "Gaussian", "GaussianSeparable", "Bilateral", "BilateralPS", "A-Trous" };
const char* GAOResolution[3] = { "100", "75", "50" };
extern const char* GAOType[3] = { "NoAO", "RTAO", "SSAO" };

static_assert(_countof(GDenoisers) == (int32)EDenoiser::MAX, "EDenoiser count mismatch");

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
	SunDir = Vector(0.31f, -0.828f, -0.241f);
	AnisoG = 0.15f;
	EarthQuake = false;
	FocalDistance = 5.0f;
	LensRadius = 0.05f;
	AORadius = 50.0f;
	SSAOBias = AORadius / 20.0f;
	AOIntensity = 1.0f;
	UseSSGI = true;
	UseSSGITemporalAccumulation = true;
	SSGIAccumBlendFactor = 0.9f;
    SSGIIntensity = 1.0f;

    // SSGI Reprojection
    UseSSGIReprojection = true;
    UseDiscontinuityWeightForSSGI = true;

    // SSGI Denoising
    SSGIDenoiser = EDenoiser::A_TROUS;
    SSGIDenoiserKernelSize = 9;
    SSGIDenoiserKernelSigma = 2.5f;
    SSGIDenoiserBilateralKernelSigma = 0.01f;
    SSGI_BlurQuality = 5;

    // SSGI A-Trous Denoising
    SSGI_A_Trous_Sigma_Color = 1.0f;
    SSGI_A_Trous_Sigma_Normal = 0.5f;
    SSGI_A_Trous_Sigma_Depth = 5.0f;

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
