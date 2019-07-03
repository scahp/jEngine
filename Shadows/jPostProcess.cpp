#include "pch.h"
#include "jPostProcess.h"
#include "jCamera.h"
#include "jRHI.h"
#include "jLight.h"
#include "jPrimitiveUtil.h"
#include "jShadowAppProperties.h"

//////////////////////////////////////////////////////////////////////////
// IPostprocess
void IPostprocess::SetUp()
{
	PostProcessOutput = std::shared_ptr<jPostProcessInOutput>(new jPostProcessInOutput());
	PostProcessOutput->RenderTaret = RenderTarget;
}

bool IPostprocess::Process(const jCamera* camera) const
{
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

void IPostprocess::SetPostProcessInput(std::weak_ptr<jPostProcessInOutput> input)
{
	PostProcessInput = input;
}

jFullscreenQuadPrimitive* IPostprocess::GetFullscreenQuad() const
{
	static jFullscreenQuadPrimitive* s_fullscreenQuad = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
	return s_fullscreenQuad;
}

void IPostprocess::Draw(const jCamera* camera, const jShader* shader, const jLight* directionalLight) const
{
	GetFullscreenQuad()->Draw(camera, shader, directionalLight);
}

//////////////////////////////////////////////////////////////////////////
// jPostprocessChain
jPostprocessChain::~jPostprocessChain()
{

}

void jPostprocessChain::AddNewPostprocess(IPostprocess* postprocess)
{
	JASSERT(postprocess);
	postprocess->SetUp();

	if (!PostProcesses.empty())
	{
		IPostprocess* lastPostProcess = *PostProcesses.rbegin();
		JASSERT(lastPostProcess);

		postprocess->SetPostProcessInput(lastPostProcess->GetPostProcessOutput());
	}
	PostProcesses.push_back(postprocess);
}

void jPostprocessChain::ReleaseAllPostprocesses()
{
	for (auto iter : PostProcesses)
		iter->TearDown();
	PostProcesses.clear();
}

bool jPostprocessChain::Process(const jCamera* camera)
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

	if (auto deepShadowFull_Shader = jShadowAppSettingProperties::GetInstance().ExponentDeepShadowOn ? jShader::GetShader("ExpDeepShadowFull") : jShader::GetShader("DeepShadowFull"))
	{
		g_rhi->SetShader(deepShadowFull_Shader);
		SSBOs.Bind(deepShadowFull_Shader);
		Draw(camera, deepShadowFull_Shader, DirectionalLight);
	}

	return true;
}

bool jPostProcess_AA_DeepShadowAddition::Do(const jCamera* camera) const
{
	JASSERT(Shader);

	g_rhi->SetClearColor(0.025f, 0.025f, 0.025f, 1.0f);
	g_rhi->SetClear(MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH }));

	auto fullscreenQuad = GetFullscreenQuad();
	if (!PostProcessInput.expired())
		fullscreenQuad->SetTexture(PostProcessInput.lock()->RenderTaret->GetTexture());
	fullscreenQuad->Draw(camera, Shader, nullptr);
	fullscreenQuad->SetTexture(nullptr);

	return true;
}
