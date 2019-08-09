#include "pch.h"
#include "jPostProcess.h"
#include "jCamera.h"
#include "jRHI.h"
#include "jLight.h"
#include "jPrimitiveUtil.h"
#include "jShadowAppProperties.h"
#include "jGBuffer.h"
#include "jRenderTargetPool.h"

//////////////////////////////////////////////////////////////////////////
// IPostprocess
void IPostprocess::Setup()
{
	PostProcessOutput = std::shared_ptr<jPostProcessInOutput>(new jPostProcessInOutput());
	PostProcessOutput->RenderTaret = RenderTarget.get();
}

bool IPostprocess::Process(const jCamera* camera) const
{
	SCOPE_DEBUG_EVENT(g_rhi, Name.c_str());

	if (RenderTarget)
		RenderTarget->Begin();
	else
		g_rhi->SetRenderTarget(nullptr);

	auto result = Do(camera);

	if (RenderTarget)
		RenderTarget->End();

	return result;
}

std::weak_ptr<jPostProcessInOutput> IPostprocess::GetPostProcessOutput() const
{
	return PostProcessOutput;
}

void IPostprocess::SetPostProcessInput(const std::weak_ptr<jPostProcessInOutput>& input)
{
	PostProcessInput = input;
}

void IPostprocess::SetPostProcessOutput(const std::shared_ptr<jPostProcessInOutput>& output)
{
	PostProcessOutput = output;
}

jFullscreenQuadPrimitive* IPostprocess::GetFullscreenQuad() const
{
	static jFullscreenQuadPrimitive* s_fullscreenQuad = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
	return s_fullscreenQuad;
}

void IPostprocess::Draw(const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights) const
{
	GetFullscreenQuad()->Draw(camera, shader, lights);
}

//////////////////////////////////////////////////////////////////////////
// jPostprocessChain
void jPostprocessChain::AddNewPostprocess(IPostprocess* postprocess)
{
	JASSERT(postprocess);
	postprocess->Setup();

	if (!PostProcesses.empty())
	{
		IPostprocess* lastPostProcess = *PostProcesses.rbegin();
		JASSERT(lastPostProcess);

		postprocess->SetPostProcessInput(lastPostProcess->GetPostProcessOutput());
	}
	PostProcesses.push_back(postprocess);
}

void jPostprocessChain::AddNewPostprocess(IPostprocess* postprocess, const std::weak_ptr<jPostProcessInOutput>& input)
{
	JASSERT(postprocess);
	postprocess->Setup();
	postprocess->SetPostProcessInput(input);
	PostProcesses.push_back(postprocess);
}

void jPostprocessChain::ReleaseAllPostprocesses()
{
	for (auto iter : PostProcesses)
		iter->TearDown();
	PostProcesses.clear();
}

bool jPostprocessChain::Process(const jCamera* camera) const
{
	for (auto iter : PostProcesses)
		iter->Process(camera);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// jPostProcess_DeepShadowMap
void jPostProcess_DeepShadowMap::jSSBO_DeepShadowMap::Bind(const jShader* shader) const
{
	JASSERT(shader);
	JASSERT(StartElementBuf);
	JASSERT(LinkedListEntryDepthAlphaNext);
	JASSERT(LinkedListEntryNeighbors);

	StartElementBuf->Bind(shader);
	LinkedListEntryDepthAlphaNext->Bind(shader);
	LinkedListEntryNeighbors->Bind(shader);
}

bool jPostProcess_DeepShadowMap::Do(const jCamera* camera) const
{
	g_rhi->SetClearColor(0.025f, 0.025f, 0.025f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	const auto ambientLight = camera->GetLight(ELightType::AMBIENT);
	const auto directionalLight = camera->GetLight(ELightType::DIRECTIONAL);

	std::list<const jLight*> lights;
	if (ambientLight)
		lights.push_back(ambientLight);
	if (directionalLight)
		lights.push_back(directionalLight);

	if (auto deepShadowFull_Shader = jShadowAppSettingProperties::GetInstance().ExponentDeepShadowOn ? jShader::GetShader("ExpDeepShadowFull") : jShader::GetShader("DeepShadowFull"))
	{
		g_rhi->SetShader(deepShadowFull_Shader);

		camera->BindCamera(deepShadowFull_Shader);
		jLight::BindLights(lights, deepShadowFull_Shader);

		SSBOs.Bind(deepShadowFull_Shader);

		JASSERT(GBuffer);
		GBuffer->BindGeometryBuffer(deepShadowFull_Shader);

		Draw(camera, deepShadowFull_Shader, lights);
	}

	return true;
}

void jPostProcess_AA_DeepShadowAddition::Setup()
{
	__super::Setup();
	Shader = jShader::GetShader("DeepShadowAA");
}

bool jPostProcess_AA_DeepShadowAddition::Do(const jCamera* camera) const
{
	JASSERT(Shader);
	JASSERT(!PostProcessInput.expired());

	g_rhi->SetClearColor(0.025f, 0.025f, 0.025f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	auto fullscreenQuad = GetFullscreenQuad();
	if (!PostProcessInput.expired())
		fullscreenQuad->SetTexture(PostProcessInput.lock()->RenderTaret->GetTexture());
	camera->BindCamera(Shader);
	fullscreenQuad->Draw(camera, Shader, {});
	fullscreenQuad->SetTexture(nullptr);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// jPostProcess_Blur
bool jPostProcess_Blur::Do(const jCamera* camera) const
{
	JASSERT(!PostProcessInput.expired());

	g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	auto shader = OmniDirectional ? jShader::GetShader("BlurOmni") : jShader::GetShader("Blur");

	auto fullscreenQuad = GetFullscreenQuad();
	if (!PostProcessInput.expired())
		fullscreenQuad->SetTexture(PostProcessInput.lock()->RenderTaret->GetTexture());

	g_rhi->SetShader(shader);
	SET_UNIFORM_BUFFER_STATIC(float, "IsVertical", IsVertical, shader);
	SET_UNIFORM_BUFFER_STATIC(float, "MaxDist", MaxDist, shader);

	fullscreenQuad->Draw(camera, shader, {});
	fullscreenQuad->SetTexture(nullptr);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// jPostProcess_Tonemap
void jPostProcess_Tonemap::Setup()
{
	__super::Setup();
	Shader = jShader::GetShader("Tonemap");
}

bool jPostProcess_Tonemap::Do(const jCamera* camera) const
{
	JASSERT(Shader);
	JASSERT(!PostProcessInput.expired());

	g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	auto fullscreenQuad = GetFullscreenQuad();
	if (!PostProcessInput.expired())
	{
		fullscreenQuad->SetTexture(PostProcessInput.lock()->RenderTaret->GetTexture());
		fullscreenQuad->SetTexture2(PostProcessInput.lock()->LuminanceMapRT->GetTexture());
	}
	camera->BindCamera(Shader);
	
	g_rhi->SetShader(Shader);
	SET_UNIFORM_BUFFER_STATIC(int, "UseTonemap", jShadowAppSettingProperties::GetInstance().UseTonemap, Shader);
	SET_UNIFORM_BUFFER_STATIC(float, "AutoExposureKeyValue", jShadowAppSettingProperties::GetInstance().AutoExposureKeyValue, Shader);

	fullscreenQuad->Draw(camera, Shader, {});
	fullscreenQuad->SetTexture(nullptr);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// jPostProcess_LuminanceMapGeneration
void jPostProcess_LuminanceMapGeneration::Setup()
{
	__super::Setup();
	Shader = jShader::GetShader("LuminanceMapGeneration");
}

bool jPostProcess_LuminanceMapGeneration::Process(const jCamera* camera) const
{
	SCOPE_DEBUG_EVENT(g_rhi, Name.c_str());

	if (RenderTarget)
		RenderTarget->Begin();
	else
		g_rhi->SetRenderTarget(nullptr);

	auto result = Do(camera);

	if (RenderTarget)
		RenderTarget->End();

	if (RenderTarget)
		g_rhi->GenerateMips(RenderTarget->GetTexture());

	return result;
}

bool jPostProcess_LuminanceMapGeneration::Do(const jCamera* camera) const
{
	JASSERT(Shader);
	JASSERT(!PostProcessInput.expired());

	g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	auto fullscreenQuad = GetFullscreenQuad();
	if (!PostProcessInput.expired())
		fullscreenQuad->SetTexture(PostProcessInput.lock()->RenderTaret->GetTexture());
	camera->BindCamera(Shader);

	fullscreenQuad->Draw(camera, Shader, {});
	fullscreenQuad->SetTexture(nullptr);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// jPostProcess_AdaptiveLuminance
int32 jPostProcess_AdaptiveLuminance::s_index = 0;
bool jPostProcess_AdaptiveLuminance::Process(const jCamera* camera) const
{
	SCOPE_DEBUG_EVENT(g_rhi, Name.c_str());
	UpdateLuminanceIndex();

	const auto pCurrentLuminanceRT = LastLumianceRenderTarget[s_index];

	if (pCurrentLuminanceRT)
		pCurrentLuminanceRT->Begin();
	else
		g_rhi->SetRenderTarget(nullptr);

	auto result = Do(camera);

	if (pCurrentLuminanceRT)
		pCurrentLuminanceRT->End();

	PostProcessOutput->LuminanceMapRT = (pCurrentLuminanceRT ? pCurrentLuminanceRT.get() : nullptr);

	return result;
}

void jPostProcess_AdaptiveLuminance::Setup()
{
	__super::Setup();
	Shader = jShader::GetShader("AdaptiveLuminance");
	LastLumianceRenderTarget[0] = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, EDepthBufferType::DEPTH, 1, 1, 1 });
	LastLumianceRenderTarget[1] = jRenderTargetPool::GetRenderTarget({ ETextureType::TEXTURE_2D, EFormat::RG32F, EFormat::RG, EFormatType::FLOAT, EDepthBufferType::DEPTH, 1, 1, 1 });
}

bool jPostProcess_AdaptiveLuminance::Do(const jCamera* camera) const
{
	JASSERT(Shader);
	JASSERT(!PostProcessInput.expired());

	g_rhi->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	auto fullscreenQuad = GetFullscreenQuad();

	JASSERT(!LuminanceMap.expired());
	fullscreenQuad->SetTexture(LuminanceMap.lock()->GetTexture());		// CurrentLuminance

	if (!PostProcessInput.expired())
		fullscreenQuad->SetTexture2(LastLumianceRenderTarget[!s_index]->GetTexture());		// LastLuminance
	camera->BindCamera(Shader);

	g_rhi->SetShader(Shader);
	SET_UNIFORM_BUFFER_STATIC(float, "TimeDeltaSecond", g_timeDeltaSecond, Shader);
	SET_UNIFORM_BUFFER_STATIC(float, "AdaptationRate", jShadowAppSettingProperties::GetInstance().AdaptationRate, Shader);

	fullscreenQuad->Draw(camera, Shader, {});
	fullscreenQuad->SetTexture(nullptr);
	fullscreenQuad->SetTexture2(nullptr);

	return true;
}
