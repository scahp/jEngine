#include "pch.h"
#include "FileLoader/jImageFileLoader.h"
#include "Material/jMaterial.h"
#include "Profiler/jPerformanceProfile.h"
#include "RHI/jRHIUtil.h"
#include "RHI/jRaytracingScene.h"
#include "RHI/jRenderFrameContext.h"
#include "RHI/jRenderTargetPool.h"
#include "RHI/jShaderBindingLayout.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Shader/jCommonShaderParameters.h"
#include "Shader/jLightingShaderParameters.h"
#include "dxcapi.h"
#include "jDirectionalLightDrawCommandGenerator.h"
#include "jGame.h"
#include "jOptions.h"
#include "jPointLightDrawCommandGenerator.h"
#include "jPrimitiveUtil.h"
#include "jRenderer.h"
#include "jSceneRenderTargets.h"
#include "jSpotLightDrawCommandGenerator.h"
#include "Shader/jShaderParameterSet.h"

#define ASYNC_WITH_SETUP 0
#define PARALLELFOR_WITH_PASSSETUP 0

struct jSimplePushConstant
{
    Vector4 Color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

    bool ShowVRSArea = false;
    bool Padding[3];

    bool ShowGrid = true;
    bool Padding2[3];
};

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jApplySSGIUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(float, g_SSGIIntensity)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_SceneWidth)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_SceneHeight)
    SHADER_UNIFORM_BUFFER_MEMBER(int32, g_ShowSSGIOnly)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jApplySSGICSParameters)
    SHADER_RW_TEXTURE2D(OutColorTexture)
    SHADER_TEXTURE2D(SceneColorTexture)
    SHADER_TEXTURE2D(SSGITexture)
    SHADER_UNIFORM_BUFFER(jApplySSGIUniformBuffer, ApplySSGIUniformBuffer)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jLinearDepthUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, InvP)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, ScreenSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, Padding)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jCalcLinearDepthCSParameters)
    SHADER_RW_TEXTURE2D(OutLinearDepthTexture)
    SHADER_TEXTURE2D(InDepthTexture)
    SHADER_UNIFORM_BUFFER(jLinearDepthUniformBuffer, ComputeParam)
END_SHADER_PARAMETER_SET()

namespace
{
template <typename TShaderParameters>
void DispatchShaderParameterComputePass(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr
    , jName InShaderName, jName InShaderFilePath, const TShaderParameters& InParameters
    , uint32 NumGroupsX, uint32 NumGroupsY, uint32 NumGroupsZ)
{
    auto CurrentBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(
        InParameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

    jShaderInfo ShaderInfo;
    ShaderInfo.SetName(InShaderName);
    ShaderInfo.SetShaderFilepath(InShaderFilePath);
    ShaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
    jShaderParameterSet::AppendToShaderInfo<TShaderParameters>(ShaderInfo, 0);
    jShader* Shader = g_rhi->CreateShader(ShaderInfo);

    jShaderBindingInstanceGroup ShaderBindingGroup;
    ShaderBindingGroup.Add(CurrentBindingInstance);
    jPipelineStateInfo* ComputePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingGroup.GetLayoutArray(), {});
    ComputePipelineStateInfo->Bind(InRenderFrameContextPtr);

    g_rhi->BindComputeShaderBindingInstances(InRenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingGroup.GetCombiner(), 0);
    g_rhi->DispatchCompute(InRenderFrameContextPtr, NumGroupsX, NumGroupsY, NumGroupsZ);
}
}

// IRenderer
void IRenderer::DebugPasses()
{
	SCOPE_CPU_PROFILE(DebugPasses);
	SCOPE_GPU_PROFILE(RenderFrameContextPtr, DebugPasses);
	DEBUG_EVENT(RenderFrameContextPtr, "DebugPasses");

	if (gOptions.ShowDebugObject)
	{
		DEBUG_EVENT(RenderFrameContextPtr, "DebugObject");

		g_rhi->TransitionLayout(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::DEPTH_STENCIL_ATTACHMENT);

		// Prepare basepass pipeline
		jRasterizationStateInfo* RasterizationState = nullptr;
		switch (g_rhi->GetSelectedMSAASamples())
		{
		case EMSAASamples::COUNT_1:
			RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
			break;
		case EMSAASamples::COUNT_2:
			RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
			break;
		case EMSAASamples::COUNT_4:
			RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
			break;
		case EMSAASamples::COUNT_8:
			RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
			break;
		default:
			check(0);
			break;
		}
		auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
		auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

		jPipelineStateFixedInfo DebugPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
			, jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

		const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
		const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

		jAttachment depth = {
			.RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr,
			.LoadStoreOp = EAttachmentLoadStoreOp::LOAD_DONTCARE,
			.StencilLoadStoreOp = EAttachmentLoadStoreOp::LOAD_DONTCARE,
			.RTClearValue = ClearDepth,
			.InitialLayout = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetLayout(),
			.FinalLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT
		};
		jAttachment resolve;

		if (RenderFrameContextPtr->UseForwardRenderer)
		{
			if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
			{
				resolve = {
					.RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->ResolvePtr,
					.LoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_STORE,
					.StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
					.RTClearValue = ClearColor,
					.InitialLayout = EResourceLayout::UNDEFINED,
					.FinalLayout = EResourceLayout::COLOR_ATTACHMENT,
					.bResolveAttachment = true
				};
			}
		}

		// Setup attachment
		jRenderPassInfo renderPassInfo;
		const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

		//if (UseForwardRenderer || gOptions.UseSubpass)
		{
			jAttachment color = {
				.RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr,
				.LoadStoreOp = EAttachmentLoadStoreOp::LOAD_STORE,
				.StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
				.RTClearValue = ClearColor,
				.InitialLayout = RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr->GetLayout(),
				.FinalLayout = EResourceLayout::COLOR_ATTACHMENT
			};
			renderPassInfo.Attachments.push_back(color);
		}

		const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
		renderPassInfo.Attachments.push_back(depth);

		const int32 ResolveAttachemntIndex = (int32)renderPassInfo.Attachments.size();
		if (RenderFrameContextPtr->UseForwardRenderer)
		{
			if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
				renderPassInfo.Attachments.push_back(resolve);
		}

		//////////////////////////////////////////////////////////////////////////
		// Setup subpass of BasePass
		{
			jSubpass subpass;
			subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
			subpass.OutputColorAttachments.push_back(0);
			subpass.OutputDepthAttachment = DepthAttachmentIndex;
			if (RenderFrameContextPtr->UseForwardRenderer)
			{
				if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
					subpass.OutputResolveAttachment = ResolveAttachemntIndex;
			}
			renderPassInfo.Subpasses.push_back(subpass);
		}
		jRenderPass* DebugRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

		jGraphicsPipelineShader DebugObjectShader;
		{
			jShaderDebugObjectVertexShader::ShaderPermutation VertexPermutation;
			DebugObjectShader.VertexShader = jShaderDebugObjectVertexShader::CreateShader(VertexPermutation);

			jShaderDebugObjectPixelShader::ShaderPermutation PixelPermutation;
			DebugObjectShader.PixelShader = jShaderDebugObjectPixelShader::CreateShader(PixelPermutation);
		}

		std::vector<jDrawCommand> DebugDrawCommand;
		auto& DebugObjects = jObject::GetDebugObject();
		DebugDrawCommand.resize(DebugObjects.size());
		for (int32 i = 0; i < (int32)DebugObjects.size(); ++i)
		{
			new (&DebugDrawCommand[i]) jDrawCommand(RenderFrameContextPtr, &View, DebugObjects[i]->RenderObjects[0], DebugRenderPass
				, DebugObjectShader, &DebugPassPipelineStateFixed, DebugObjects[i]->RenderObjects[0]->MaterialPtr.get(), jShaderBindingInstanceArray(), nullptr);
			DebugDrawCommand[i].PrepareToDraw(false);
		}

		if (DebugRenderPass && DebugRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
		{
			for (auto& command : DebugDrawCommand)
			{
				command.Draw();
			}
			DebugRenderPass->EndRenderPass();
		}
	}

	{
		DEBUG_EVENT(RenderFrameContextPtr, "DebugRenderTarget");

		if (DebugRTs.size() > 0)
		{
			const int32 RTWidth = RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr->Info.Width;
			const int32 RTHeight = RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr->Info.Height;

			Vector2i Size(RTWidth / 4, RTHeight / 4);
			Vector2i Padding(10, 10);
			for (int32 i = 0; i < (int32)DebugRTs.size(); ++i)
			{
				const Vector4i DrawRect = Vector4i(RTWidth - Size.x - Padding.x, RTHeight - Size.y * (i + 1) - Padding.y, Size.x, Size.y);
				jRHIUtil::DrawQuad(RenderFrameContextPtr, RenderFrameContextPtr->SceneRenderTargetPtr->FinalColorPtr, DrawRect
					, [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
					{
						jTexture* InTexture = DebugRTs[i].get();
						g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

						const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
							, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
							, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

						jRHIUtil::BuildSingleTextureFragmentBindings(InTexture, SamplerState, InOutShaderBindingArray, InOutResourceInlineAllactor);
					}
					, [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
						{
							jShaderInfo shaderInfo;
							shaderInfo.SetName(jNameStatic("CopyPS"));
							shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
							shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
							jRHIUtil::AppendSingleTextureFragmentShaderInfo(shaderInfo);
							return g_rhi->CreateShader(shaderInfo);
						});
			}
		}
	}
}

// jRenderer
void jRenderer::Setup()
{
    SCOPE_CPU_PROFILE(Setup);

    FrameIndex = g_rhi->GetCurrentFrameIndex();
    View.ShadowCasterLights.reserve(View.Lights.size());

    // Build per-light view data
    for (int32 i = 0; i < View.Lights.size(); ++i)
    {
        jViewLight& ViewLight = View.Lights[i];

        if (ViewLight.Light)
        {
            if (ViewLight.Light->IsShadowCaster)
            {
                ViewLight.ShadowMapPtr = RenderFrameContextPtr->SceneRenderTargetPtr->GetShadowMap(ViewLight.Light);
            }

            ViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(ViewLight.ShadowMapPtr ? ViewLight.ShadowMapPtr->GetTexture() : nullptr);

            if (ViewLight.Light->IsShadowCaster)
            {
                jViewLight NewViewLight = ViewLight;
                NewViewLight.ShaderBindingInstance = ViewLight.Light->PrepareShaderBindingInstance(nullptr);
                View.ShadowCasterLights.push_back(NewViewLight);
            }
        }
    }

#if ASYNC_WITH_SETUP
    ShadowPassSetupCompleteEvent = std::async(std::launch::async, &jRenderer::SetupShadowPass, this);
#else
    jRenderer::SetupShadowPass();
    jRenderer::SetupBasePass();
#endif

    if (g_rhi->RaytracingScene && g_rhi->RaytracingScene->ShouldUpdate())
    {
        SCOPE_CPU_PROFILE(UpdateTLAS);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, UpdateTLAS);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "UpdateTLAS", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

        auto CmdBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();
        jRatracingInitializer InInitializer;
        InInitializer.CommandBuffer = CmdBuffer;
        InInitializer.RenderObjects = jObject::GetStaticRenderObject();
        g_rhi->RaytracingScene->CreateOrUpdateTLAS(InInitializer);
    }
}

void jRenderer::SetupShadowPass()
{
    SCOPE_CPU_PROFILE(SetupShadowPass);

    ShadowDrawInfo.reserve(View.Lights.size());

    for (int32 i = 0; i < View.ShadowCasterLights.size(); ++i)
    {
        jViewLight& ViewLight = View.ShadowCasterLights[i];
        if (!ViewLight.Light->IsShadowCaster)
            continue;
        if (!ViewLight.ShadowMapPtr)
            continue;

        ShadowDrawInfo.push_back(jShadowDrawInfo());
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[ShadowDrawInfo.size() - 1];

        const bool IsUseReverseZShadow = USE_REVERSEZ_PERSPECTIVE_SHADOW && (ViewLight.Light->IsUseRevereZPerspective());

        // Prepare shadowpass pipeline
        auto RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f
            , false, false, EMSAASamples::COUNT_1, true, 0.2f, false, false>::Create();
        auto DepthStencilState = IsUseReverseZShadow ? TDepthStencilStateInfo<true, true, ECompareOp::GREATER, false, false, 0.0f, 1.0f>::Create()
            : TDepthStencilStateInfo<true, true, ECompareOp::LEQUAL, false, false, 0.0f, 1.0f>::Create();
        auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE
            , EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::NONE>::Create();

        const int32 RTWidth = ViewLight.ShadowMapPtr->Info.Width;
        const int32 RTHeight = ViewLight.ShadowMapPtr->Info.Height;

        jPipelineStateFixedInfo ShadpwPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
            , jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), false);

        {
            const jRTClearValue ClearDepth = IsUseReverseZShadow ? jRTClearValue(0.0f, 0) : jRTClearValue(1.0f, 0);

            jAttachment depth = {
                .RenderTargetPtr = ViewLight.ShadowMapPtr,
                .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
                .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
                .RTClearValue = ClearDepth,
                .InitialLayout = EResourceLayout::UNDEFINED,
                .FinalLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT
            };

            // Setup attachment
            jRenderPassInfo renderPassInfo;
            renderPassInfo.Attachments.push_back(depth);

            // Setup subpass of ShadowPass
            jSubpass subpass;
            subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
            subpass.OutputDepthAttachment = 0;
            renderPassInfo.Subpasses.push_back(subpass);

            ShadowPasses.ShadowMapRenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { RTWidth, RTHeight });
        }

        // Shadow 패스에서 사용할 ViewLight를 보관한다. 이후 Light와 Shadow Camera 정보를 재사용한다.
        ShadowPasses.ViewLight = ViewLight;

        jGraphicsPipelineShader ShadowShader;
        {
            if (ViewLight.Light->IsOmnidirectional())
            {
                jShaderOmniShadowVertexShader::ShaderPermutation VertexPermutation;
                ShadowShader.VertexShader = jShaderOmniShadowVertexShader::CreateShader(VertexPermutation);

                jShaderOmniShadowPixelShader::ShaderPermutation PixelPermutation;
                ShadowShader.PixelShader = jShaderOmniShadowPixelShader::CreateShader(PixelPermutation);
            }
            else
            {
                if (ViewLight.Light->Type == ELightType::SPOT)
                {
                    jShaderSpotShadowVertexShader::ShaderPermutation VertexPermutation;
                    ShadowShader.VertexShader = jShaderSpotShadowVertexShader::CreateShader(VertexPermutation);

                    jShaderShadowPixelShader::ShaderPermutation PixelPermutation;
                    ShadowShader.PixelShader = jShaderShadowPixelShader::CreateShader(PixelPermutation);
                }
                else
                {
                    jShaderDirectionalShadowVertexShader::ShaderPermutation VertexPermutation;
                    ShadowShader.VertexShader = jShaderDirectionalShadowVertexShader::CreateShader(VertexPermutation);

                    jShaderShadowPixelShader::ShaderPermutation PixelPermutation;
                    ShadowShader.PixelShader = jShaderShadowPixelShader::CreateShader(PixelPermutation);
                }
            }
        }
        jGraphicsPipelineShader ShadowInstancingShader;
        {
            jShaderShadowInstancingVertexShader::ShaderPermutation VertexPermutation;
            ShadowInstancingShader.VertexShader = jShaderShadowInstancingVertexShader::CreateShader(VertexPermutation);

            jShaderShadowPixelShader::ShaderPermutation PixelPermutation;
            ShadowInstancingShader.PixelShader = jShaderShadowPixelShader::CreateShader(PixelPermutation);
        }

#if PARALLELFOR_WITH_PASSSETUP
        ShadowPasses.DrawCommands.resize(jObject::GetShadowCasterRenderObject().size());
        jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetShadowCasterRenderObject()
            , [&](size_t InIndex, jRenderObject* InRenderObject)
            {
                jMaterial* Material = nullptr;

                // todo : Masked material need to set Material for jDrawCommand
                // iter->MaterialPtr;

                const bool ShouldUseOnePassPointLightShadow = (ViewLight.Light->Type == ELightType::POINT);
                const jVertexBuffer* OverrideInstanceData = (ShouldUseOnePassPointLightShadow ? jRHI::CubeMapInstanceDataForSixFace.get() : nullptr);

                new (&ShadowPasses.DrawCommands[InIndex]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, InRenderObject, ShadowPasses.ShadowMapRenderPass
                    , (InRenderObject->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, Material, jShaderBindingInstanceArray(), nullptr, OverrideInstanceData);
                ShadowPasses.DrawCommands[InIndex].PrepareToDraw(true);
            });
#else
        ShadowPasses.DrawCommands.resize(jObject::GetShadowCasterRenderObject().size());
        {
            int32 i = 0;
            for (auto iter : jObject::GetShadowCasterRenderObject())
            {
                jMaterial* Material = nullptr;

                // todo : Masked material need to set Material for jDrawCommand
                // iter->MaterialPtr;

                const bool ShouldUseOnePassPointLightShadow = (ViewLight.Light->Type == ELightType::POINT);
                const jVertexBuffer* OverrideInstanceData = (ShouldUseOnePassPointLightShadow ? jRHI::CubeMapInstanceDataForSixFace.get() : nullptr);

                new (&ShadowPasses.DrawCommands[i]) jDrawCommand(RenderFrameContextPtr, &ShadowPasses.ViewLight, iter, ShadowPasses.ShadowMapRenderPass
                    , (iter->HasInstancing() ? ShadowInstancingShader : ShadowShader), &ShadpwPipelineStateFixed, Material, jShaderBindingInstanceArray(), nullptr, OverrideInstanceData);
                ShadowPasses.DrawCommands[i].PrepareToDraw(true);
                ++i;
            }
        }
#endif
    }
}

void jRenderer::SetupBasePass()
{
    SCOPE_CPU_PROFILE(SetupBasePass);

    // Prepare basepass pipeline
    jRasterizationStateInfo* RasterizationState = nullptr;
    switch (g_rhi->GetSelectedMSAASamples())
    {
    case EMSAASamples::COUNT_1:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_2:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)2, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_4:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)4, true, 0.2f, false, false>::Create();
        break;
    case EMSAASamples::COUNT_8:
        RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)8, true, 0.2f, false, false>::Create();
        break;
    default:
        check(0);
        break;
    }
    auto DepthStencilState = TDepthStencilStateInfo<true, true, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo BasePassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    auto TranslucentBlendingState = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    jPipelineStateFixedInfo TranslucentPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, TranslucentBlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

    jAttachment depth = {
        .RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr,
        .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
        .StencilLoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
        .RTClearValue = ClearDepth,
        .InitialLayout = EResourceLayout::UNDEFINED,
        .FinalLayout = EResourceLayout::DEPTH_STENCIL_ATTACHMENT
    };
    jAttachment resolve;

    if (RenderFrameContextPtr->UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
        {
            resolve = {
                .RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->ResolvePtr,
                .LoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_STORE,
                .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
                .RTClearValue = ClearColor,
                .InitialLayout = EResourceLayout::UNDEFINED,
                .FinalLayout = EResourceLayout::COLOR_ATTACHMENT,
                .bResolveAttachment = true
            };
        }
    }

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    if (!RenderFrameContextPtr->UseForwardRenderer)
    {
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
        {
            jAttachment color = {
                .RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i],
                .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
                .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
                .RTClearValue = ClearColor,
                .InitialLayout = EResourceLayout::UNDEFINED,
                .FinalLayout = EResourceLayout::COLOR_ATTACHMENT
            };
            renderPassInfo.Attachments.push_back(color);
        }
    }

    const int32 LightPassAttachmentIndex = (int32)renderPassInfo.Attachments.size();

    if (RenderFrameContextPtr->UseForwardRenderer || gOptions.UseSubpass)
    {
        jAttachment color = {
            .RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr,
            .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
            .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
            .RTClearValue = ClearColor,
            .InitialLayout = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetLayout(),
            .FinalLayout = EResourceLayout::COLOR_ATTACHMENT
        };
        renderPassInfo.Attachments.push_back(color);
    }

    const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
    renderPassInfo.Attachments.push_back(depth);

    const int32 ResolveAttachemntIndex = (int32)renderPassInfo.Attachments.size();
    if (RenderFrameContextPtr->UseForwardRenderer)
    {
        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            renderPassInfo.Attachments.push_back(resolve);
    }

    //////////////////////////////////////////////////////////////////////////
    // Setup subpass of BasePass
    {
        // First subpass, Geometry pass
        jSubpass subpass;
        subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);

        if (RenderFrameContextPtr->UseForwardRenderer)
        {
            subpass.OutputColorAttachments.push_back(0);
        }
        else
        {
            const int32 GBufferCount = LightPassAttachmentIndex;
            for (int32 i = 0; i < GBufferCount; ++i)
            {
                subpass.OutputColorAttachments.push_back(i);
            }
        }

        subpass.OutputDepthAttachment = DepthAttachmentIndex;
        if (RenderFrameContextPtr->UseForwardRenderer)
        {
            if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
                subpass.OutputResolveAttachment = ResolveAttachemntIndex;
        }
        renderPassInfo.Subpasses.push_back(subpass);
    }
    if (!RenderFrameContextPtr->UseForwardRenderer && gOptions.UseSubpass)
    {
        // Second subpass, Lighting pass
        jSubpass subpass;
        subpass.Initialize(1, 2, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);

        const int32 GBufferCount = LightPassAttachmentIndex;
        for (int32 i = 0; i < GBufferCount; ++i)
        {
            subpass.InputAttachments.push_back(i);
        }
        subpass.InputAttachments.push_back(DepthAttachmentIndex);

        subpass.OutputColorAttachments.push_back(LightPassAttachmentIndex);
        subpass.OutputDepthAttachment = DepthAttachmentIndex;
        subpass.DepthAttachmentReadOnly = true;

        if ((int32)g_rhi->GetSelectedMSAASamples() > 1)
            subpass.OutputResolveAttachment = ResolveAttachemntIndex;

        renderPassInfo.Subpasses.push_back(subpass);
    }
    //////////////////////////////////////////////////////////////////////////
    BaseRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

    auto GetOrCreateShaderFunc = [UseForwardRenderer = RenderFrameContextPtr->UseForwardRenderer](const jRenderObject* InRenderObject, const jMaterial* InOverrideMaterial = nullptr)
        {
            jGraphicsPipelineShader Shaders;
            jShaderInfo shaderInfo;

            if (InRenderObject->HasInstancing())
            {
                check(UseForwardRenderer);
                jShaderForwardInstancingVertexShader::ShaderPermutation VertexShaderPermutation;
                Shaders.VertexShader = jShaderForwardInstancingVertexShader::CreateShader(VertexShaderPermutation);

                jShaderForwardPixelShader::ShaderPermutation ShaderPermutation;
                ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
                ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_REVERSEZ>(USE_REVERSEZ_PERSPECTIVE_SHADOW);
                Shaders.PixelShader = jShaderForwardPixelShader::CreateShader(ShaderPermutation);
                return Shaders;
            }

            if (UseForwardRenderer)
            {
                jShaderForwardVertexShader::ShaderPermutation VertexShaderPermutation;
                Shaders.VertexShader = jShaderForwardVertexShader::CreateShader(VertexShaderPermutation);

                jShaderForwardPixelShader::ShaderPermutation ShaderPermutation;
                ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
                ShaderPermutation.SetIndex<jShaderForwardPixelShader::USE_REVERSEZ>(USE_REVERSEZ_PERSPECTIVE_SHADOW);
                Shaders.PixelShader = jShaderForwardPixelShader::CreateShader(ShaderPermutation);
            }
            else
            {
                const jMaterial* Material = InOverrideMaterial ? InOverrideMaterial : InRenderObject->MaterialPtr.get();
                const bool IsUseSphericalMap = Material && Material->IsUseSphericalMap();
                const bool HasAlbedoTexture = Material && Material->HasAlbedoTexture();
                const bool IsUseSRGBAlbedoTexture = Material && Material->IsUseSRGBAlbedoTexture();
                const bool HasVertexColor = InRenderObject->GeometryDataPtr && InRenderObject->GeometryDataPtr->HasVertexColor();
                const bool HasVertexBiTangent = InRenderObject->GeometryDataPtr && InRenderObject->GeometryDataPtr->HasVertexBiTangent();

                jShaderGBufferVertexShader::ShaderPermutation ShaderPermutationVS;
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_COLOR>(HasVertexColor);
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_BITANGENT>(HasVertexBiTangent);
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_ALBEDO_TEXTURE>(HasAlbedoTexture);
                ShaderPermutationVS.SetIndex<jShaderGBufferVertexShader::USE_SPHERICAL_MAP>(IsUseSphericalMap);
                Shaders.VertexShader = jShaderGBufferVertexShader::CreateShader(ShaderPermutationVS);

                jShaderGBufferPixelShader::ShaderPermutation ShaderPermutationPS;
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_VERTEX_COLOR>(HasVertexColor);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_ALBEDO_TEXTURE>(HasAlbedoTexture);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_SRGB_ALBEDO_TEXTURE>(IsUseSRGBAlbedoTexture);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
                ShaderPermutationPS.SetIndex<jShaderGBufferPixelShader::USE_PBR>(ENABLE_PBR);
                Shaders.PixelShader = jShaderGBufferPixelShader::CreateShader(ShaderPermutationPS);
            }
            return Shaders;
        };

    jGraphicsPipelineShader TranslucentPassShader;
    {
        jShaderGBufferVertexShader::ShaderPermutation VertexShaderPermutation;
        VertexShaderPermutation.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_COLOR>(0);
        VertexShaderPermutation.SetIndex<jShaderGBufferVertexShader::USE_VERTEX_BITANGENT>(0);
        VertexShaderPermutation.SetIndex<jShaderGBufferVertexShader::USE_ALBEDO_TEXTURE>(1);
        VertexShaderPermutation.SetIndex<jShaderGBufferVertexShader::USE_SPHERICAL_MAP>(0);
        TranslucentPassShader.VertexShader = jShaderGBufferVertexShader::CreateShader(VertexShaderPermutation);

        jShaderGBufferPixelShader::ShaderPermutation ShaderPermutation;
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_VERTEX_COLOR>(0);
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_ALBEDO_TEXTURE>(1);
        ShaderPermutation.SetIndex<jShaderGBufferPixelShader::USE_VARIABLE_SHADING_RATE>(USE_VARIABLE_SHADING_RATE_TIER2);
        TranslucentPassShader.PixelShader = jShaderGBufferPixelShader::CreateShader(ShaderPermutation);
    }

    jSimplePushConstant SimplePushConstantData;
    SimplePushConstantData.ShowVRSArea = gOptions.ShowVRSArea;
    SimplePushConstantData.ShowGrid = gOptions.ShowGrid;

    jPushConstant* SimplePushConstant = new(jMemStack::Get()->Alloc<jPushConstant>()) jPushConstant(SimplePushConstantData, EShaderAccessStageFlag::FRAGMENT);

#if PARALLELFOR_WITH_PASSSETUP
    BasePasses.resize(jObject::GetStaticRenderObject().size());
    jParallelFor::ParallelForWithTaskPerThread(MaxPassSetupTaskPerThreadCount, jObject::GetStaticRenderObject()
        , [&](size_t InIndex, jRenderObject* InRenderObject)
        {
            jMaterial* Material = nullptr;
            if (InRenderObject->MaterialPtr)
            {
                Material = InRenderObject->MaterialPtr.get();
            }
            else
            {
                if (GDefaultMaterial)
                {
                    Material = GDefaultMaterial.get();
                }
            }

            new (&BasePasses[InIndex]) jDrawCommand(RenderFrameContextPtr, &View, InRenderObject, BaseRenderPass
                , GetOrCreateShaderFunc(InRenderObject, Material), &BasePassPipelineStateFixed, Material, jShaderBindingInstanceArray(), SimplePushConstant);
            BasePasses[InIndex].PrepareToDraw(false);
        });
#else
    BasePasses.resize(jObject::GetStaticRenderObject().size());
    int32 i = 0;
    for (auto iter : jObject::GetStaticRenderObject())
    {
        jMaterial* Material = nullptr;
        if (iter->MaterialPtr)
        {
            Material = iter->MaterialPtr.get();
        }
        else
        {
            if (GDefaultMaterial)
            {
                Material = GDefaultMaterial.get();
            }
        }

        //Material = GDefaultMaterial.get();

        new (&BasePasses[i]) jDrawCommand(RenderFrameContextPtr, &View, iter, BaseRenderPass
            , GetOrCreateShaderFunc(iter, Material), &BasePassPipelineStateFixed, Material, jShaderBindingInstanceArray(), SimplePushConstant);
        BasePasses[i].PrepareToDraw(false);
        ++i;
    }
#endif
}

void jRenderer::PrepareHistoryDepth()
{
    if (gOptions.HasAnyReprojection())
    {
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "CopyDepthBuffer", Vector4(0.8f, 0.0f, 0.0f, 1.0f));
        SCOPE_CPU_PROFILE(CopyDepthBuffer);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, CopyDepthBuffer);

        jRHIUtil::CopyTexture2D(RenderFrameContextPtr
            , jSceneRenderTarget::HistoryDepthBuffer.get()
            , RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture());
    }
}

void jRenderer::ShadowPass()
{
#if ASYNC_WITH_SETUP
    if (ShadowPassSetupCompleteEvent.valid())
        ShadowPassSetupCompleteEvent.wait();

    BasePassSetupCompleteEvent = std::async(std::launch::async, &jRenderer::SetupBasePass, this);
#endif

    SCOPE_CPU_PROFILE(ShadowPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, ShadowPass);
    DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "ShadowPass", Vector4(0.8f, 0.0f, 0.0f, 1.0f));

    for (int32 i = 0; i < ShadowDrawInfo.size(); ++i)
    {
        jShadowDrawInfo& ShadowPasses = ShadowDrawInfo[i];

        const char* ShadowPassEventName = nullptr;
        switch (ShadowPasses.ViewLight.Light->Type)
        {
        case ELightType::DIRECTIONAL:
            ShadowPassEventName = "DirectionalLight";
            break;
        case ELightType::POINT:
            ShadowPassEventName = "PointLight";
            break;
        case ELightType::SPOT:
            ShadowPassEventName = "SpotLight";
            break;
        }

        DEBUG_EVENT(RenderFrameContextPtr, ShadowPassEventName);

        const std::shared_ptr<jRenderTarget>& ShadowMapPtr = ShadowPasses.GetShadowMapPtr();

        {
            auto NewLayout = ShadowMapPtr->GetTexture()->IsDepthOnlyFormat() ? EResourceLayout::DEPTH_ATTACHMENT : EResourceLayout::DEPTH_STENCIL_ATTACHMENT;
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), NewLayout);
        }

        if (ShadowPasses.ShadowMapRenderPass && ShadowPasses.ShadowMapRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            //ShadowpassOcclusionTest.BeginQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
            for (const auto& command : ShadowPasses.DrawCommands)
            {
                command.Draw();
            }
            //ShadowpassOcclusionTest.EndQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
            ShadowPasses.ShadowMapRenderPass->EndRenderPass();
        }

        {
            auto NewLayout = ShadowMapPtr->GetTexture()->IsDepthOnlyFormat() ? EResourceLayout::DEPTH_READ_ONLY : EResourceLayout::DEPTH_STENCIL_READ_ONLY;
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ShadowMapPtr->GetTexture(), NewLayout);
        }
    }
}

void jRenderer::BasePass()
{
    if (BasePassSetupCompleteEvent.valid())
        BasePassSetupCompleteEvent.wait();

    const bool UseHWRTDirectLighting = IsUseHWRTDirectLighting();

    {
        SCOPE_CPU_PROFILE(BasePass);
        SCOPE_GPU_PROFILE(RenderFrameContextPtr, BasePass);
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "BasePass", Vector4(0.8f, 0.8f, 0.0f, 1.0f));

        if (RenderFrameContextPtr->UseForwardRenderer)
        {
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        }
        else
        {
            for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
            {
                g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i]->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
            }
        }

        {
            auto NewLayout = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture()->IsDepthOnlyFormat() ? EResourceLayout::DEPTH_ATTACHMENT : EResourceLayout::DEPTH_STENCIL_ATTACHMENT;
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), NewLayout);
        }

        //BasepassOcclusionTest.BeginQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
        if (BaseRenderPass && BaseRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
        {
            // Draw G-Buffer : subpass 0 
            for (const auto& command : BasePasses)
            {
                command.Draw();
            }

            // Draw Light : subpass 1
            if (!RenderFrameContextPtr->UseForwardRenderer && gOptions.UseSubpass && !UseHWRTDirectLighting)
            {
                g_rhi->NextSubpass(RenderFrameContextPtr->GetActiveCommandBuffer());
                DeferredLightPass_TodoRefactoring(BaseRenderPass);
            }

            BaseRenderPass->EndRenderPass();
        }

        if (!RenderFrameContextPtr->UseForwardRenderer && !gOptions.UseSubpass)
        {
            if (UseHWRTDirectLighting)
            {
                if (!HWRTDirectLightingPass())
                {
                    DeferredLightPass_TodoRefactoring(BaseRenderPass);
                }
            }
            else
            {
                DeferredLightPass_TodoRefactoring(BaseRenderPass);
            }
        }
        else if (!RenderFrameContextPtr->UseForwardRenderer && UseHWRTDirectLighting)
        {
            HWRTDirectLightingPass();
        }
        //BasepassOcclusionTest.EndQuery(RenderFrameContextPtr->GetActiveCommandBuffer());
    }
}

void jRenderer::DeferredLightPass_TodoRefactoring(jRenderPass* InRenderPass)
{
    SCOPE_CPU_PROFILE(LightingPass);
    SCOPE_GPU_PROFILE(RenderFrameContextPtr, LightingPass);
    DEBUG_EVENT(RenderFrameContextPtr, "LightingPass");

    if (!gOptions.UseSubpass)
    {
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::DEPTH_STENCIL_READ_ONLY);
        for (int32 i = 0; i < _countof(RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer); ++i)
        {
            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->GBuffer[i]->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // GBuffer input attachment 바인딩
    jShaderBindingInstanceGroup DefaultLightPassShaderBindingGroup;
    DefaultLightPassShaderBindingGroup.Add(View.ViewUniformBufferShaderBindingInstance);
    //////////////////////////////////////////////////////////////////////////

    std::vector<jDrawCommand> LightPasses;
    LightPasses.reserve(View.Lights.size());
    std::vector<const jViewLight*> DrawableLights;
    DrawableLights.reserve(View.Lights.size());

    const int32 RTWidth = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Width;
    const int32 RTHeight = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info.Height;

    // Create a separate DrawCommandGenerator for each light
    std::vector<std::unique_ptr<jDrawCommandGenerator>> LightDrawCommandGenerators;
    LightDrawCommandGenerators.reserve(View.Lights.size());

    for (int32 i = 0; i < (int32)View.Lights.size(); ++i)
    {
        const auto& viewLight = View.Lights[i];
        std::unique_ptr<jDrawCommandGenerator> generator;

        switch (viewLight.Light->Type)
        {
        case ELightType::DIRECTIONAL:
            generator = std::make_unique<jDirectionalLightDrawCommandGenerator>(DefaultLightPassShaderBindingGroup);
            break;
        case ELightType::POINT:
            generator = std::make_unique<jPointLightDrawCommandGenerator>(DefaultLightPassShaderBindingGroup);
            break;
        case ELightType::SPOT:
            generator = std::make_unique<jSpotLightDrawCommandGenerator>(DefaultLightPassShaderBindingGroup);
            break;
        default:
            continue;
        }

        if (generator)
        {
            generator->Initialize(RTWidth, RTHeight);
            LightDrawCommandGenerators.push_back(std::move(generator));
            LightPasses.push_back(jDrawCommand());
            DrawableLights.push_back(&viewLight);
        }
    }

    {
        const int32 SubpassIndex = (!RenderFrameContextPtr->UseForwardRenderer && gOptions.UseSubpass) ? 1 : 0;

        if (!gOptions.UseSubpass)
        {
            const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
            const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

            jAttachment depth = {
                .RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr,
                .LoadStoreOp = EAttachmentLoadStoreOp::LOAD_STORE,
                .StencilLoadStoreOp = EAttachmentLoadStoreOp::LOAD_DONTCARE,
                .RTClearValue = ClearDepth,
                .InitialLayout = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetLayout(),
                .FinalLayout = EResourceLayout::DEPTH_STENCIL_READ_ONLY
            };

            // Setup attachment
            jRenderPassInfo renderPassInfo;
            jAttachment color = {
                .RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr,
                .LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
                .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
                .RTClearValue = ClearColor,
                .InitialLayout = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetLayout(),
                .FinalLayout = EResourceLayout::COLOR_ATTACHMENT
            };
            renderPassInfo.Attachments.push_back(color);

            const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
            renderPassInfo.Attachments.push_back(depth);

            // Setup subpass of LightingPass
            jSubpass subpass;
            subpass.SourceSubpassIndex = 0;
            subpass.DestSubpassIndex = 1;

            for (int32 i = 0; i < DepthAttachmentIndex; ++i)
            {
                subpass.OutputColorAttachments.push_back(i);
            }

            subpass.OutputDepthAttachment = DepthAttachmentIndex;
            subpass.DepthAttachmentReadOnly = true;

            subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
            renderPassInfo.Subpasses.push_back(subpass);

            InRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

            check(InRenderPass);
            InRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer());
        }

        check(DrawableLights.size() == LightPasses.size());
        check(LightDrawCommandGenerators.size() == LightPasses.size());
        for (int32 i = 0; i < (int32)LightPasses.size(); ++i)
        {
            LightDrawCommandGenerators[i]->GenerateDrawCommand(&LightPasses[i], RenderFrameContextPtr, &View, *DrawableLights[i], InRenderPass, SubpassIndex);
        }

        for (int32 i = 0; i < (int32)LightPasses.size(); ++i)
        {
            LightPasses[i].Draw();
        }

        if (!gOptions.UseSubpass)
        {
            check(InRenderPass);
            InRenderPass->EndRenderPass();
        }
    }
}

void jRenderer::Render()
{
	SCOPE_CPU_PROFILE(Render);

    const bool UseHWRTDirectLighting = IsUseHWRTDirectLighting();

    {
        SCOPE_CPU_PROFILE(PoolReset);
        check(RenderFrameContextPtr->GetActiveCommandBuffer());
        for (int32 i = 0; i < (int32)ECommandBufferType::MAX; ++i)
        {
            if (g_rhi->GetQueryTimePool((ECommandBufferType)i))
            {
                g_rhi->GetQueryTimePool((ECommandBufferType)i)->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
            }
        }
        if (g_rhi->GetQueryOcclusionPool())
        {
            g_rhi->GetQueryOcclusionPool()->ResetQueryPool(RenderFrameContextPtr->GetActiveCommandBuffer());
        }

        // Vulkan need to queue submmit to reset query pool, and replace CurrentSemaphore with GraphicQueueSubmitSemaphore
        RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, false);
        RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();

        //ShadowpassOcclusionTest.Init();
        //BasepassOcclusionTest.Init();
    }



#if 1
    jSceneRenderTarget::CubeEnvMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_cubemp.dds")).lock().get();
    jSceneRenderTarget::IrradianceMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_irradiancemap.dds")).lock().get();
    jSceneRenderTarget::FilteredEnvMap2 = jImageFileLoader::GetInstance().LoadTextureFromFile(jNameStatic("Resource/stpeters_probe_filteredenvmap.dds")).lock().get();
#else
    static jName FilePath = jName("Resource/stpeters_probe.hdr");
    static auto Cubemap = jRHIUtil::ConvertToCubeMap(jNameStatic("F:\\Cubemap.dds"), { 512, 512 }, RenderFrameContextPtr, FilePath);
    static auto IrradianceMap = jRHIUtil::GenerateIrradianceMap(jNameStatic("F:\\IrradianceMap.dds"), { 256, 256 }, RenderFrameContextPtr, Cubemap->GetTexture());
    static auto EnvironmentCubeMap = jRHIUtil::GenerateFilteredEnvironmentMap(jNameStatic("F:\\FilteredEnvMap.dds"), { 256, 256 }, RenderFrameContextPtr, Cubemap->GetTexture());

    jSceneRenderTarget::CubeEnvMap2 = Cubemap->GetTexture();
    jSceneRenderTarget::IrradianceMap2 = IrradianceMap->GetTexture();
    jSceneRenderTarget::FilteredEnvMap2 = EnvironmentCubeMap->GetTexture();
#endif

    {
        Setup();
        ShadowPass();

        // Queue submit to prepare shadowmap for basepass 
        if (gOptions.QueueSubmitAfterShadowPass)
        {
            SCOPE_CPU_PROFILE(QueueSubmitAfterShadowPass);
            RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::ShadowPass, false);
            RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
        }

        BasePass();

        // Calculate linear depth
        {
            DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "CalcLinearDepth", Vector4(0.0f, 0.5f, 0.8f, 1.0f));
            SCOPE_CPU_PROFILE(CalcLinearDepth);
            SCOPE_GPU_PROFILE(RenderFrameContextPtr, CalcLinearDepth);

            jLinearDepthUniformBuffer UniformData;
            UniformData.InvP = jCamera::GetMainCamera()->Projection.GetInverse();
            UniformData.ScreenSize.x = (float)SCR_WIDTH;
            UniformData.ScreenSize.y = (float)SCR_HEIGHT;

            auto UniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("LinearDepthUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformData));
            UniformBuffer->UpdateBufferData(&UniformData, sizeof(UniformData));

            g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

            jCalcLinearDepthCSParameters Parameters;
            Parameters.OutLinearDepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture() };
            Parameters.InDepthTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), nullptr };
            Parameters.ComputeParam.Buffer = std::shared_ptr<IUniformBufferBlock>(UniformBuffer);

            DispatchShaderParameterComputePass(RenderFrameContextPtr
                , jNameStatic("CalcLinearDepth_CS")
                , jNameStatic("Resource/Shaders/hlsl/CalcLinearDepth_cs.hlsl")
                , Parameters
                , RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture()->Width / 8 + ((RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture()->Width % 8) ? 1 : 0)
                , RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture()->Height / 8 + ((RenderFrameContextPtr->SceneRenderTargetPtr->LinearDepthPtr->GetTexture()->Height % 8) ? 1 : 0)
                , 1);
        }

        // Queue submit to prepare scenecolor RT for postprocess
        if (gOptions.QueueSubmitAfterBasePass)
        {
            SCOPE_CPU_PROFILE(QueueSubmitAfterBasePass);
            RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::BasePass, false);
            RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();
        }

        // Object picking pass (runs on mouse click only)
        HitObjectPass();
    }

    {
        PrepareHistoryDepth();

        if (!UseHWRTDirectLighting)
        {
            AOPass();
            SurfelGIPass();
            SurfelGIResolvePass();
            SSGIPass();
            SSGIAccumulatePass();
        }
    }

    // Apply SSGI
    if (!UseHWRTDirectLighting && gOptions.UseSSGI && jSceneRenderTarget::SSGI_RT)
    {
        DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "ApplySSGI", Vector4(0.0f, 0.5f, 0.8f, 1.0f));

        // Select the source SSGI texture
        auto ssgiRenderTarget = gOptions.UseSSGITemporalAccumulation
            ? jSceneRenderTarget::SSGI_Accum_RT[RenderFrameContextPtr->FrameIndex % 3]
            : jSceneRenderTarget::SSGI_RT;
        auto ssgiTexture = ssgiRenderTarget->GetTexturePtr();

        // Denoise the SSGI texture if enabled
        if (gOptions.SSGIDenoiser != EDenoiser::NONE)     // GDenoisers[3] is "None"
        {
            ssgiTexture = BlurSSGI(ssgiRenderTarget);
        }

        // To avoid read/write hazard on ColorPtr, copy it to a temp texture.
        auto TempColorRT = jRenderTargetPool::GetRenderTargetForOneFrame(RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->Info);
        jRHIUtil::DrawQuad(RenderFrameContextPtr, TempColorRT, {0, 0, SCR_WIDTH, SCR_HEIGHT},
            [&](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
            {
                jTexture* InTexture = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture();
                g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), InTexture, EResourceLayout::SHADER_READ_ONLY);

                const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                    , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                    , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

                jRHIUtil::BuildSingleTextureFragmentBindings(InTexture, SamplerState, InOutShaderBindingArray, InOutResourceInlineAllactor);
            },
            [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
            {
                jShaderInfo shaderInfo;
                shaderInfo.SetName(jNameStatic("CopyPS"));
                shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/copy_ps.hlsl"));
                shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
                jRHIUtil::AppendSingleTextureFragmentShaderInfo(shaderInfo);
                return g_rhi->CreateShader(shaderInfo);
            }
        );

        jApplySSGIUniformBuffer UniformData;
        UniformData.g_SSGIIntensity = gOptions.SSGIIntensity;
        UniformData.g_SceneWidth = SCR_WIDTH;
        UniformData.g_SceneHeight = SCR_HEIGHT;
        UniformData.g_ShowSSGIOnly = gOptions.ShowSSGIOnly ? 1 : 0;

        auto UniformBuffer = g_rhi->CreateUniformBufferBlock(jNameStatic("ApplySSGIUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformData));
        UniformBuffer->UpdateBufferData(&UniformData, sizeof(UniformData));

        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), TempColorRT->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), ssgiTexture.get(), EResourceLayout::SHADER_READ_ONLY);

        const jSamplerStateInfo* SSGISamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
            , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

        jApplySSGICSParameters Parameters;
        Parameters.OutColorTexture = { RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture() };
        Parameters.SceneColorTexture = { TempColorRT->GetTexture(), nullptr };
        Parameters.SSGITexture = { ssgiTexture.get(), SSGISamplerState };
        Parameters.ApplySSGIUniformBuffer.Buffer = std::shared_ptr<IUniformBufferBlock>(UniformBuffer);

        DispatchShaderParameterComputePass(RenderFrameContextPtr
            , jNameStatic("ApplySSGI_CS")
            , jNameStatic("Resource/Shaders/hlsl/ApplySSGI_cs.hlsl")
            , Parameters
            , RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Width / 8 + ((RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Width % 8) ? 1 : 0)
            , RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Height / 8 + ((RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()->Height % 8) ? 1 : 0)
            , 1);
    }

    if (!UseHWRTDirectLighting)
    {
        ApplySurfelGI();
    }
    
    AtmosphericShadow();

    PostProcess();

    DebugPasses();
    UIPass();
}


