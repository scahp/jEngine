#include "pch.h"
#include "jPipeline.h"
#include "jObject.h"
#include "jLight.h"
#include "jCamera.h"
#include "jGBuffer.h"

const std::list<jObject*> jPipelineData::emptyObjectList;
//const std::list<const jLight*> jPipelineData::emptyLightList;

//////////////////////////////////////////////////////////////////////////
// jRenderPipeline
void jRenderPipeline::Do(const jPipelineData& pipelineData)
{
	Draw(pipelineData.Objects, Shader, pipelineData.Camera, pipelineData.Lights);
}

void jRenderPipeline::Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const
{
	g_rhi->SetClearColor(ClearColor);
	g_rhi->SetClear(ClearType);

	g_rhi->EnableDepthTest(EnableDepthTest);
	g_rhi->SetDepthFunc(DepthStencilFunc);

	g_rhi->EnableBlend(EnableBlend);
	g_rhi->SetBlendFunc(BlendSrc, BlendDest);

	g_rhi->EnableDepthBias(EnableDepthBias);
	g_rhi->SetDepthBias(DepthConstantBias, DepthSlopeBias);

	g_rhi->SetShader(shader);

	for (const auto& iter : Buffers)
		iter->Bind(shader);

	for (const auto& iter : objects)
		iter->Draw(camera, shader, lights);
}

//////////////////////////////////////////////////////////////////////////
// jDeferredGeometryPipeline
void jDeferredGeometryPipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	Shader = jShader::GetShader("Deferred");			// jShader::GetShader("ExpDeferred");
}

void jDeferredGeometryPipeline::Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const
{
	JASSERT(GBuffer);
	if (GBuffer->Begin())
	{
		__super::Draw(objects, Shader, camera, lights);
		GBuffer->End();
	}
}

//////////////////////////////////////////////////////////////////////////
// jDeepShadowMap_ShadowPass_Pipeline
void jDeepShadowMap_ShadowPass_Pipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	EnableDepthTest = true;
	DepthStencilFunc = EDepthStencilFunc::LEQUAL;
	EnableDepthBias = true;
	DepthSlopeBias = 1.0f;
	DepthConstantBias = 1.0f;
	Shader = jShader::GetShader("DeepShadowMapGen");		// jShader::GetShader("ExpDeepShadowMapGen")

	Buffers.push_back(DeepShadowMapBuffers.AtomicBuffer);
	Buffers.push_back(DeepShadowMapBuffers.StartElementBuf);
	Buffers.push_back(DeepShadowMapBuffers.LinkedListEntryDepthAlphaNext);
}

void jDeepShadowMap_ShadowPass_Pipeline::Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const
{
	for (auto light : lights)
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
			g_rhi->SetShader(Shader);

			__super::Draw(objects, Shader, lightCamera, { nullptr });
			renderTarget->End();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// jForwardShadowMap_SSM_Pipeline
void jForwardShadowMap_SSM_Pipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	EnableDepthTest = true;
	DepthStencilFunc = EDepthStencilFunc::LEQUAL;
	ShadowGenShader = jShader::GetShader("ShadowGen_SSM");
	OmniShadowGenShader = jShader::GetShader("ShadowGen_Omni_SSM");
}

void jForwardShadowMap_SSM_Pipeline::Teardown()
{

}

void jForwardShadowMap_SSM_Pipeline::Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const
{
	//light->Update(0); // todo remove

	for (auto light : lights)
	{
		JASSERT(light);

		bool useMRT = false;
		bool skip = false;

		jShader* currentShader = nullptr;
		switch (light->Type)
		{
		case ELightType::DIRECTIONAL:
			currentShader = ShadowGenShader;
			break;
		case ELightType::POINT:
		case ELightType::SPOT:
			currentShader = OmniShadowGenShader;
			useMRT = true;
			break;
		case ELightType::AMBIENT:
			skip = true;
			break;
		default:
			JASSERT(0);
			return;
		}

		if (skip)
			continue;

		light->RenderToShadowMap([&objects, currentShader, light, this](const jRenderTarget* renderTarget
			, int32 renderTargetIndex, const jCamera* camera, const std::vector<jViewport>& viewports)
			{
				g_rhi->SetRenderTarget(renderTarget, renderTargetIndex);
				if (viewports.empty())
					g_rhi->SetViewport({ 0, 0, SM_WIDTH, SM_HEIGHT });
				else
					g_rhi->SetViewportIndexedArray(0, static_cast<int32>(viewports.size()), &viewports[0]);
				this->jRenderPipeline::Draw(objects, currentShader, camera, { light });
			}, currentShader);
	}

	g_rhi->SetRenderTarget(nullptr);
}

//////////////////////////////////////////////////////////////////////////
// jForward_SSM_Pipeline
void jForward_SSM_Pipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	EnableBlend = true;
	DepthStencilFunc = EDepthStencilFunc::LEQUAL;
	EnableBlend = true;
	BlendSrc = EBlendSrc::ONE;
	BlendDest = EBlendDest::ZERO;
	Shader = jShader::GetShader("SSM");
}

void jForward_SSM_Pipeline::Teardown()
{
}

void jForward_SSM_Pipeline::Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const
{
	g_rhi->SetRenderTarget(nullptr);
	__super::Draw(objects, Shader, camera, lights);
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

static auto s_FuncSetup = [](std::list<IPipeline*>& pipelines)
{
	for (auto& pipeline : pipelines)
		pipeline->Setup();
};

static auto s_FuncTeardown = [](std::list<IPipeline*>& pipelines)
{
	for (auto& pipeline : pipelines)
		pipeline->Teardown();
};

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

	s_FuncSetup(ShadowPrePass);
	s_FuncSetup(RenderPass);
	s_FuncSetup(PostRenderPass);
}

void jDeferredDeepShadowMapPipelineSet::Teardown()
{
	s_FuncTeardown(ShadowPrePass);
	s_FuncTeardown(RenderPass);
	s_FuncTeardown(PostRenderPass);
}

//////////////////////////////////////////////////////////////////////////
// jForwardPipelineSet
void jForwardPipelineSet::Setup()
{
	// Setup ShadowPrePass
	ShadowPrePass.push_back(new jForwardShadowMap_SSM_Pipeline());
	RenderPass.push_back(new jForward_SSM_Pipeline());

	s_FuncSetup(ShadowPrePass);
	s_FuncSetup(RenderPass);
}

void jForwardPipelineSet::Teardown()
{
	s_FuncTeardown(ShadowPrePass);
	s_FuncTeardown(RenderPass);
}
