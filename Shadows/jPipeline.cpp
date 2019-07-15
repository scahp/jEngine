#include "pch.h"
#include "jPipeline.h"
#include "jObject.h"
#include "jLight.h"
#include "jCamera.h"
#include "jGBuffer.h"
#include "jRenderTargetPool.h"
#include "jPostProcess.h"

const std::list<jObject*> jPipelineData::emptyObjectList;
//const std::list<const jLight*> jPipelineData::emptyLightList;

std::unordered_map<std::string, IPipeline*> IPipeline::s_pipelinesNameMap;
std::unordered_set<IPipeline*> IPipeline::s_pipelinesNameSet;

void IPipeline::AddPipeline(const std::string& name, IPipeline* newPipeline)
{
	s_pipelinesNameMap.insert(std::make_pair(name, newPipeline));
	s_pipelinesNameSet.insert(newPipeline);
}

IPipeline* IPipeline::GetPipeline(const std::string& name)
{
	auto it_find = s_pipelinesNameMap.find(name);
	return s_pipelinesNameMap.end() != it_find ? it_find->second : nullptr;
}

void IPipeline::SetupPipelines()
{
	for (auto it : s_pipelinesNameMap)
	{
		if (it.second)
			it.second->Setup();
	}
}

void IPipeline::TeardownPipelines()
{
	for (auto it : s_pipelinesNameMap)
	{
		if (it.second)
			it.second->Teardown();
	}
}

struct jShadowPipelinCreation
{
	jShadowPipelinCreation()
	{
		ADD_FORWARD_SHADOWMAP_GEN_PIPELINE(Forward_ShadowMapGen_SSM_Pipeline, "ShadowGen_SSM", "ShadowGen_Omni_SSM");
		ADD_FORWARD_SHADOWMAP_GEN_PIPELINE(Forward_ShadowMapGen_VSM_Pipeline, "ShadowGen_VSM", "ShadowGen_Omni_SSM");
		ADD_FORWARD_SHADOWMAP_GEN_PIPELINE(Forward_ShadowMapGen_ESM_Pipeline, "ShadowGen_ESM", "ShadowGen_Omni_ESM");
		ADD_FORWARD_SHADOWMAP_GEN_PIPELINE(Forward_ShadowMapGen_EVSM_Pipeline, "ShadowGen_EVSM", "ShadowGen_Omni_EVSM");


		ADD_FORWARD_SHADOW_PIPELINE(Forward_SSM_Pipeline, "SSM");
		ADD_FORWARD_SHADOW_PIPELINE(Forward_SSM_PCF_Pipeline, "SSM_PCF");
		ADD_FORWARD_SHADOW_PIPELINE(Forward_SSM_PCSS_Pipeline, "SSM_PCSS");
		ADD_FORWARD_SHADOW_PIPELINE(Forward_SSM_PCF_Poisson_Pipeline, "SSM_PCF_Poisson");
		ADD_FORWARD_SHADOW_PIPELINE(Forward_SSM_PCSS_Poisson_Pipeline, "SSM_PCSS_Poisson");
		ADD_FORWARD_SHADOW_PIPELINE(Forward_VSM_Pipeline, "VSM");
		ADD_FORWARD_SHADOW_PIPELINE(Forward_ESM_Pipeline, "ESM");
		ADD_FORWARD_SHADOW_PIPELINE(Forward_EVSM_Pipeline, "EVSM");
	}
} s_shadowPipelinCreation;

//////////////////////////////////////////////////////////////////////////
// jRenderPipeline
void jRenderPipeline::Do(const jPipelineData& pipelineData) const
{
	Draw(pipelineData, Shader);
}

void jRenderPipeline::Draw(const jPipelineData& pipelineData, const jShader* shader) const
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

	for (const auto& iter : pipelineData.Objects)
		iter->Draw(pipelineData.Camera, shader, pipelineData.Lights);
}

//////////////////////////////////////////////////////////////////////////
// jDeferredGeometryPipeline
void jDeferredGeometryPipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	Shader = jShader::GetShader("Deferred");			// jShader::GetShader("ExpDeferred");
}

void jDeferredGeometryPipeline::Draw(const jPipelineData& pipelineData, const jShader* shader) const
{
	JASSERT(GBuffer);
	if (GBuffer->Begin())
	{
		__super::Draw(pipelineData, Shader);
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

void jDeepShadowMap_ShadowPass_Pipeline::Draw(const jPipelineData& pipelineData, const jShader* shader) const
{
	for (auto light : pipelineData.Lights)
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

			__super::Draw(jPipelineData(pipelineData.Objects, lightCamera, {}), Shader);
			renderTarget->End();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// jForward_ShadowMapGen_Pipeline
void jForward_ShadowMapGen_Pipeline::Setup()
{
	ClearColor = Vector4(FLT_MAX, FLT_MAX, FLT_MAX, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	EnableDepthTest = true;
	DepthStencilFunc = EDepthStencilFunc::LEQUAL;
	ShadowGenShader = jShader::GetShader(DirectionalLightShaderName);
	JASSERT(ShadowGenShader);
	OmniShadowGenShader = jShader::GetShader(OmniDirectionalLightShaderName);
	JASSERT(OmniShadowGenShader);
}

void jForward_ShadowMapGen_Pipeline::Draw(const jPipelineData& pipelineData, const jShader* shader) const
{
	//light->Update(0); // todo remove

	for (auto light : pipelineData.Lights)
	{
		JASSERT(light);

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

		light->RenderToShadowMap([&pipelineData, currentShader, light, this](const jRenderTarget* renderTarget
			, int32 renderTargetIndex, const jCamera* camera, const std::vector<jViewport>& viewports)
			{
				g_rhi->SetRenderTarget(renderTarget, renderTargetIndex);
				if (viewports.empty())
					g_rhi->SetViewport({ 0, 0, SM_WIDTH, SM_HEIGHT });
				else
					g_rhi->SetViewportIndexedArray(0, static_cast<int32>(viewports.size()), &viewports[0]);
				this->jRenderPipeline::Draw(jPipelineData(pipelineData.Objects, camera, {light}), currentShader);
			}, currentShader);
	}

	g_rhi->SetRenderTarget(nullptr);
}

//////////////////////////////////////////////////////////////////////////
// jForwardShadowMap_Blur_Pipeline
void jForwardShadowMap_Blur_Pipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	EnableDepthTest = false;
	DepthStencilFunc = EDepthStencilFunc::LEQUAL;

	PostProcessInput = std::shared_ptr<jPostProcessInOutput>(new jPostProcessInOutput());
}

void jForwardShadowMap_Blur_Pipeline::Draw(const jPipelineData& pipelineData, const jShader* shader) const
{
	for (auto light : pipelineData.Lights)
	{
		JASSERT(light);

		bool skip = false;
		bool isOmniDirectional = false;
		const jShader* shader = nullptr;
		switch (light->Type)
		{
		case ELightType::DIRECTIONAL:
			isOmniDirectional = false;
			break;
		case ELightType::POINT:
		case ELightType::SPOT:
			isOmniDirectional = true;
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

		JASSERT(light->GetShadowMapRenderTarget());
		auto tempRenderTargetPtr = jRenderTargetPool::GetRenderTarget(light->GetShadowMapRenderTarget()->Info);
		
		// todo postprocess should alloc once.
		PostProcessInput->RenderTaret = light->GetShadowMapRenderTarget();
		auto post1 = new jPostProcess_Blur("BlurVer", tempRenderTargetPtr, true);
		post1->OmniDirectional = isOmniDirectional;
		post1->SetPostProcessInput(PostProcessInput);
		
		post1->Process(pipelineData.Camera);

		auto post2 = new jPostProcess_Blur("BlurHor", light->GetShadowMapRenderTargetPtr(), false);
		PostProcessInput->RenderTaret = tempRenderTargetPtr.get();
		post2->SetPostProcessInput(PostProcessInput);
		post2->OmniDirectional = isOmniDirectional;

		post2->Process(pipelineData.Camera);

		delete post1;
		delete post2;

		jRenderTargetPool::ReturnRenderTarget(tempRenderTargetPtr.get());
	}
}

//////////////////////////////////////////////////////////////////////////
// jForward_Shadow_Pipeline
void jForward_Shadow_Pipeline::Setup()
{
	ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ClearType = MakeRenderBufferTypeList({ ERenderBufferType::COLOR, ERenderBufferType::DEPTH });
	EnableBlend = true;
	DepthStencilFunc = EDepthStencilFunc::LEQUAL;
	EnableBlend = true;
	BlendSrc = EBlendSrc::ONE;
	BlendDest = EBlendDest::ZERO;
	Shader = jShader::GetShader(ShaderName);
}

//////////////////////////////////////////////////////////////////////////
// jComputePipeline
void jComputePipeline::Do(const jPipelineData& pipelineData) const
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
	ADD_PIPELINE_WITH_CREATE_AND_SETUP_AT_RENDERPASS(ShadowPrePass, jDeepShadowMap_ShadowPass_Pipeline, DeepShadowMapBuffers);
	ADD_PIPELINE_WITH_CREATE_AND_SETUP_AT_RENDERPASS(RenderPass, jDeferredGeometryPipeline, GBuffer);
	ADD_PIPELINE_WITH_CREATE_AND_SETUP_AT_RENDERPASS(PostRenderPass, jDeepShadowMap_Sort_ComputePipeline, DeepShadowMapBuffers);
	ADD_PIPELINE_WITH_CREATE_AND_SETUP_AT_RENDERPASS(PostRenderPass, jDeepShadowMap_Link_ComputePipeline, DeepShadowMapBuffers);
}

