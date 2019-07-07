#include "pch.h"
#include "jPipeline.h"
#include "jObject.h"
#include "jLight.h"
#include "jCamera.h"
#include "jGBuffer.h"

const std::list<jObject*> jPipelineData::emptyList;

//////////////////////////////////////////////////////////////////////////
// jRenderPipeline
void jRenderPipeline::Do(const jPipelineData& pipelineData)
{
	Draw(pipelineData.Objects, pipelineData.Camera, pipelineData.Light);
}

void jRenderPipeline::Draw(const std::list<jObject*>& objects, const jCamera* camera, const jLight* light) const
{
	g_rhi->SetClearColor(ClearColor);
	g_rhi->SetClear(ClearType);

	g_rhi->SetShader(Shader);

	for (const auto& iter : Buffers)
		iter->Bind(Shader);

	for (const auto& iter : objects)
		iter->Draw(camera, Shader, light);
}

//////////////////////////////////////////////////////////////////////////
// jDeferredGeometryPipeline
void jDeferredGeometryPipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	Shader = jShader::GetShader("Deferred");			// jShader::GetShader("ExpDeferred");
}

void jDeferredGeometryPipeline::Draw(const std::list<jObject*>& objects, const jCamera* camera, const jLight* light) const
{
	JASSERT(GBuffer);
	if (GBuffer->Begin())
	{
		__super::Draw(objects, camera, light);
		GBuffer->End();
	}
}

//////////////////////////////////////////////////////////////////////////
// jDeepShadowMap_ShadowPass_Pipeline
void jDeepShadowMap_ShadowPass_Pipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	Shader = jShader::GetShader("DeepShadowMapGen");		// jShader::GetShader("ExpDeepShadowMapGen")

	Buffers.push_back(DeepShadowMapBuffers.AtomicBuffer);
	Buffers.push_back(DeepShadowMapBuffers.StartElementBuf);
	Buffers.push_back(DeepShadowMapBuffers.LinkedListEntryDepthAlphaNext);
}

void jDeepShadowMap_ShadowPass_Pipeline::Draw(const std::list<jObject*>& objects, const jCamera* camera, const jLight* light) const
{
	JASSERT(light);
	JASSERT(light->GetShadowMapRenderTarget());
	JASSERT(light->GetLightCamra());
	const auto renderTarget = light->GetShadowMapRenderTarget();
	const auto lightCamera = light->GetLightCamra();

	DeepShadowMapBuffers.AtomicBuffer->ClearBuffer(0);
	DeepShadowMapBuffers.StartElementBuf->ClearBuffer(-1);

	if (renderTarget->Begin())
	{
		g_rhi->EnableDepthTest(true);
		g_rhi->SetDepthFunc(EDepthStencilFunc::LEQUAL);
		g_rhi->EnableDepthBias(true);
		g_rhi->SetDepthBias(1.0f, 1.0f);

		g_rhi->SetShader(Shader);

		__super::Draw(objects, lightCamera, light);

		g_rhi->EnableDepthBias(false);
		renderTarget->End();
	}
}

//////////////////////////////////////////////////////////////////////////
// jComputePipeline
void jComputePipeline::Do(const jPipelineData& pipelineData)
{
	Dispatch();
}

void jComputePipeline::Dispatch() const
{
	g_rhi->SetShader(Shader);

	for (const auto& iter : Buffers)
		iter->Bind(Shader);

	g_rhi->DispatchCompute(NumGroupsX, NumGroupsY, NumGroupsZ);
}

//////////////////////////////////////////////////////////////////////////
// jDeepShadowMap_Sort_ComputePipeline
void jDeepShadowMap_Sort_ComputePipeline::Setup()
{
	Shader = jShader::GetShader("cs_sort");
	ShadowMapWidthUniform = new jUniformBuffer<int>("ShadowMapWidth", SM_WIDTH);
	ShadowMapHeightUniform = new jUniformBuffer<int>("ShadowMapHeight", SM_HEIGHT);

	Buffers.push_back(ShadowMapWidthUniform);
	Buffers.push_back(ShadowMapHeightUniform);
	Buffers.push_back(DeepShadowMapBuffers.AtomicBuffer);
	Buffers.push_back(DeepShadowMapBuffers.StartElementBuf);
	Buffers.push_back(DeepShadowMapBuffers.LinkedListEntryDepthAlphaNext);

	NumGroupsX = SM_WIDTH / 16;
	NumGroupsY = SM_HEIGHT / 8;
	NumGroupsZ = 1;
}

void jDeepShadowMap_Sort_ComputePipeline::Teardown()
{
	delete ShadowMapWidthUniform;
	delete ShadowMapHeightUniform;
}

//////////////////////////////////////////////////////////////////////////
// jDeepShadowMap_Sort_ComputePipeline
void jDeepShadowMap_Link_ComputePipeline::Setup()
{
	Shader = jShader::GetShader("cs_link");
	
	ShadowMapWidthUniform = new jUniformBuffer<int>("ShadowMapWidth", SM_WIDTH);
	ShadowMapHeightUniform = new jUniformBuffer<int>("ShadowMapHeight", SM_HEIGHT);

	Buffers.push_back(ShadowMapWidthUniform);
	Buffers.push_back(ShadowMapHeightUniform);
	Buffers.push_back(DeepShadowMapBuffers.AtomicBuffer);
	Buffers.push_back(DeepShadowMapBuffers.StartElementBuf);
	Buffers.push_back(DeepShadowMapBuffers.LinkedListEntryDepthAlphaNext);
	Buffers.push_back(DeepShadowMapBuffers.LinkedListEntryNeighbors);

	NumGroupsX = SM_WIDTH / 16;
	NumGroupsY = SM_HEIGHT / 8;
	NumGroupsZ = 1;
}

void jDeepShadowMap_Link_ComputePipeline::Teardown()
{
	delete ShadowMapWidthUniform;
	delete ShadowMapHeightUniform;
}

//////////////////////////////////////////////////////////////////////////
// jDeferredDeepShadowMapPipelineSet
void jDeferredDeepShadowMapPipelineSet::Setup()
{
	// Setup SSBO
	DeepShadowMapBuffers.LinkedListEntryDepthAlphaNext = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("LinkedListEntryDepthAlphaNext"));
	DeepShadowMapBuffers.LinkedListEntryDepthAlphaNext->UpdateBufferData(nullptr, jDeepShadowMapBuffers::LinkedListDepthSize * (sizeof(float) * 2 + sizeof(uint32) * 2));

	DeepShadowMapBuffers.StartElementBuf = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("StartElementBufEntry"));
	DeepShadowMapBuffers.StartElementBuf->UpdateBufferData(nullptr, jDeepShadowMapBuffers::LinkedListDepthSize * sizeof(int32));

	DeepShadowMapBuffers.LinkedListEntryNeighbors = static_cast<jShaderStorageBufferObject_OpenGL*>(g_rhi->CreateShaderStorageBufferObject("LinkedListEntryNeighbors"));
	DeepShadowMapBuffers.LinkedListEntryNeighbors->UpdateBufferData(nullptr, jDeepShadowMapBuffers::LinkedListDepthSize * sizeof(int32) * 2);

	DeepShadowMapBuffers.AtomicBuffer = static_cast<jAtomicCounterBuffer_OpenGL*>(g_rhi->CreateAtomicCounterBuffer("LinkedListCounter", 3));

	uint32 zero = 0;
	DeepShadowMapBuffers.AtomicBuffer->UpdateBufferData(&zero, sizeof(zero));

	// Setup ShadowPrePass
	ShadowPrePass.push_back(new jDeepShadowMap_ShadowPass_Pipeline(DeepShadowMapBuffers));
	RenderPass.push_back(new jDeferredGeometryPipeline(GBuffer));
	PostRenderPass.push_back(new jDeepShadowMap_Sort_ComputePipeline(DeepShadowMapBuffers));
	PostRenderPass.push_back(new jDeepShadowMap_Link_ComputePipeline(DeepShadowMapBuffers));

	auto funcSetup = [](std::list<IPipeline*>& pipelines)
	{
		for (auto& pipeline : pipelines)
			pipeline->Setup();
	};

	funcSetup(ShadowPrePass);
	funcSetup(RenderPass);
	funcSetup(PostRenderPass);
	funcSetup(PostRenderPass);
}

void jDeferredDeepShadowMapPipelineSet::Teardown()
{
	auto funcTeardown = [](std::list<IPipeline*>& pipelines)
	{
		for (auto& pipeline : pipelines)
			pipeline->Teardown();
	};

	funcTeardown(ShadowPrePass);
	funcTeardown(RenderPass);
	funcTeardown(PostRenderPass);
	funcTeardown(PostRenderPass);
}
