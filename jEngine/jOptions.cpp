#include "pch.h"
#include "jOptions.h"

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
	BloomEyeAdaptation = true;
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
	RayPerPixel = 1;
	AOType = 1;						// Select RTAO default
	UseAOReprojection = true;
	Denoiser = GDenoisers[2];       // Select Bilateral Filter
	GaussianKernelSize = 5;
	GaussianKernelSigma = 3.0f;
	BilateralKernelSigma = 0.015f;
	ShowAOOnly = false;
	ShowDebugRT = false;
	UseAccumulateRay = true;
	UseDiscontinuityWeight = true;
	UseHaltonJitter = true;
	UseResolution = GAOResolution[2];	// Default to 1/4 size of the screen
}

bool jOptions::IsRTAO() const
{
    return 1 == AOType && GSupportRaytracing;
}

bool jOptions::IsSSAO() const
{
    return 2 == AOType;
}

bool jOptions::operator==(struct jOptions const& RHS) const
{
	static_assert(std::is_trivially_copyable<jOptions>::value, "jOptions should be trivially copyable!");

	return 0 == memcmp(this, &RHS, sizeof(jOptions));
}
