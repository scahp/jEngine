#include "pch.h"
#include "jOptions.h"

std::vector<std::string> gPathTracingScenes;
std::vector<std::string> gPathTracingScenesNameOnly;
const char* gSelectedScene = nullptr;
int32 gSelectedSceneIndex = 0;

const char* GDenoisers[4] = { "Gaussian", "GaussianSeparable", "Bilateral", "None" };
const char* GAOResolution[3] = { "100", "75", "50" };
extern const char* GAOType[3] = { "NoAO", "RTAO", "SSAO" };

jOptions gOptions;

jOptions::jOptions()
	// RHI options
	: EnableDebuggerLayer(false)
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

    // SSGI Denoising
    UseSSGIDenoising = false;
    SSGIDenoiser = GDenoisers[0];
    SSGIDenoiserKernelSize = 9;
    SSGIDenoiserKernelSigma = 2.5f;
    SSGIDenoiserBilateralKernelSigma = 0.01f;
    SSGI_BlurQuality = 3;

    // AO
    Denoiser = GDenoisers[0];
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

bool jOptions::operator==(struct jOptions const& RHS) const
{
	static_assert(std::is_trivially_copyable<jOptions>::value, "jOptions should be trivially copyable!");

	return 0 == memcmp(this, &RHS, sizeof(jOptions));
}
