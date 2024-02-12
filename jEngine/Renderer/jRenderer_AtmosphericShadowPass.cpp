#include "pch.h"
#include "jRenderer.h"
#include "Scene/Light/jLight.h"
#include "Scene/jCamera.h"
#include "jSceneRenderTargets.h"
#include "jDrawCommand.h"
#include "jPrimitiveUtil.h"
#include "Profiler/jPerformanceProfile.h"
#include "Scene/Light/jDirectionalLight.h"
#include "jOptions.h"
#include "Scene/jRenderObject.h"
#include "RHI/jRHIUtil.h"

void jRenderer::AtmosphericShadow()
{
	if (gOptions.ShowAOOnly)
		return;

    jDirectionalLight* DirectionalLight = nullptr;
    for (auto light : jLight::GetLights())
    {
        if (light->Type == ELightType::DIRECTIONAL)
        {
            DirectionalLight = (jDirectionalLight*)light;
            break;
        }
    }
    check(DirectionalLight);

    std::shared_ptr<jRenderTarget> AtmosphericShadowing = RenderFrameContextPtr->SceneRenderTargetPtr->AtmosphericShadowing;
    auto ShadowMapTexture = RenderFrameContextPtr->SceneRenderTargetPtr->GetShadowMap(DirectionalLight)->GetTexture();
    check(ShadowMapTexture);

	// Compute Test 용
    static bool UseAsyncCompute = false;
	if (UseAsyncCompute)
	{
		// Todo : Invalid flag for async compute queue
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture(), EResourceLayout::UAV);
	}
    g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EResourceLayout::UAV);

	std::shared_ptr<jSyncAcrossCommandQueue> SyncAcrossCommandQueuePtr = RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, false);
    std::shared_ptr<jRenderFrameContext> RenderFrameContextAsyncPtr = UseAsyncCompute ? RenderFrameContextPtr->CreateRenderFrameContextAsync(SyncAcrossCommandQueuePtr) : RenderFrameContextPtr;

    // https://www.gamedev.net/forums/topic/673079-error-using-resource-barrier-from-multiple-commandlists-for-same-resource/
	// AsyncComputeQueue doesn't have any ability for transition to SHADER_READ_ONLY.
	{
		check(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture()->GetLayout() == EResourceLayout::SHADER_READ_ONLY
			|| RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture()->GetLayout() == EResourceLayout::DEPTH_STENCIL_READ_ONLY);
		check(ShadowMapTexture->GetLayout() == EResourceLayout::SHADER_READ_ONLY
			|| ShadowMapTexture->GetLayout() == EResourceLayout::DEPTH_READ_ONLY
			|| ShadowMapTexture->GetLayout() == EResourceLayout::DEPTH_STENCIL_READ_ONLY);
		//check(AtmosphericShadowing->GetLayout() == EResourceLayout::UAV);
        //auto CommandBuffer = g_rhi_dx12->BeginSingleTimeCommands();
        //g_rhi->TransitionLayout(RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
        //g_rhi->TransitionLayout(ShadowMapTexture, EResourceLayout::SHADER_READ_ONLY);
        //g_rhi->TransitionLayout(AtmosphericShadowing->GetTexture(), EResourceLayout::UAV);
        //g_rhi->GetGlobalBarrierBatcher()->Flush(CommandBuffer);
        //g_rhi_dx12->EndSingleTimeCommands(CommandBuffer);

        //g_rhi_dx12->WaitForGPU();
    }

	{
		jCamera* MainCamera = jCamera::GetMainCamera();
		check(MainCamera);

		SCOPE_CPU_PROFILE(AtmosphericShadowing);
		SCOPE_GPU_PROFILE(RenderFrameContextAsyncPtr, AtmosphericShadowing);
		DEBUG_EVENT_WITH_COLOR(RenderFrameContextAsyncPtr, "AtmosphericShadowing", Vector4(0.8f, 0.8f, 0.8f, 1.0f));

		int32 Width = AtmosphericShadowing->Info.Width;
		int32 Height = AtmosphericShadowing->Info.Height;

		struct jAtmosphericData
		{
			Matrix ShadowVP;
			Matrix VP;
			Vector CameraPos;
			float CameraNear;
			Vector LightCameraDirection;
			float CameraFar;
			float AnisoG;
			float SlopeOfDist;
			float InScatteringLambda;
			float Dummy;
			int TravelCount;
			int RTWidth;
			int RTHeight;
			int UseNoise;  // todo : change to #define USE_NOISE
		};

		auto LightCamera = DirectionalLight->GetLightCamra();
		auto ShadowVP = DirectionalLight->GetLightData().ShadowVP;
		auto LightCameraDirection = LightCamera->GetForwardVector();

		jAtmosphericData AtmosphericData;
		AtmosphericData.ShadowVP = ShadowVP;
		AtmosphericData.VP = MainCamera->GetViewProjectionMatrix();
		AtmosphericData.CameraPos = MainCamera->Pos;
		AtmosphericData.LightCameraDirection = LightCameraDirection;
		AtmosphericData.CameraFar = MainCamera->Far;
		AtmosphericData.CameraNear = MainCamera->Near;
		AtmosphericData.AnisoG = gOptions.AnisoG;
		AtmosphericData.SlopeOfDist = 0.25f;
		AtmosphericData.InScatteringLambda = 0.001f;
		AtmosphericData.TravelCount = 64;
		AtmosphericData.RTWidth = Width;
		AtmosphericData.RTHeight = Height;
		AtmosphericData.UseNoise = true;

		std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
		int32 BindingPoint = 0;
		jShaderBindingArray ShaderBindingArray;
		jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

        auto CommandBuffer = RenderFrameContextAsyncPtr->GetActiveCommandBuffer();

		// Binding 0
		{
			ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
				, ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextAsyncPtr->SceneRenderTargetPtr->GBuffer[0]->GetTexture(), nullptr)));
		}

		// Binding 1
		{
			ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
				, ResourceInlineAllactor.Alloc<jTextureResource>(RenderFrameContextAsyncPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), nullptr)));
		}

		// Binding 2
		{
			ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
				, ResourceInlineAllactor.Alloc<jTextureResource>(ShadowMapTexture, nullptr)));
		}

		// Binding 3        
		auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(
			g_rhi->CreateUniformBufferBlock(jNameStatic("AtmosphericDataUniformBuffer"), jLifeTimeType::OneFrame, sizeof(AtmosphericData)));
		OneFrameUniformBuffer->UpdateBufferData(&AtmosphericData, sizeof(AtmosphericData));
		ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
			, ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));

		// Binding 4
		{
			ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
				, ResourceInlineAllactor.Alloc<jTextureResource>(AtmosphericShadowing->GetTexture(), nullptr)));
		}

		CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

		jShaderInfo shaderInfo;
		shaderInfo.SetName(jNameStatic("GenIrradianceMap"));
		shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AtmosphericShadowing_cs.hlsl"));
		shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
		jShader* Shader = g_rhi->CreateShader(shaderInfo);

		jShaderBindingLayoutArray ShaderBindingLayoutArray;
		ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

		jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

		computePipelineStateInfo->Bind(RenderFrameContextAsyncPtr);

		jShaderBindingInstanceArray ShaderBindingInstanceArray;
		ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

		jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
		for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
		{
			// Add ShaderBindingInstanceCombiner data : DescriptorSets, DynamicOffsets
			ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
			const std::vector<uint32>* pDynamicOffsetTest = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
			if (pDynamicOffsetTest && pDynamicOffsetTest->size())
			{
				ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)pDynamicOffsetTest->data(), (int32)pDynamicOffsetTest->size());
			}
		}
		ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

		g_rhi->BindComputeShaderBindingInstances(CommandBuffer, computePipelineStateInfo, ShaderBindingInstanceCombiner, 0);

		int32 X = (Width / 16) + ((Width % 16) ? 1 : 0);
		int32 Y = (Height / 16) + ((Height % 16) ? 1 : 0);
		g_rhi->DispatchCompute(RenderFrameContextAsyncPtr, X, Y, 1);
	}

    if (UseAsyncCompute)
    {
		static bool UseAsyncComputeQueue = false;
        if (UseAsyncCompute && !UseAsyncComputeQueue)
        {
            auto ComputeSyncAcrossCommandQueuePtr = RenderFrameContextAsyncPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, false);
            ComputeSyncAcrossCommandQueuePtr->WaitSyncAcrossCommandQueue(ECommandBufferType::GRAPHICS);
        }

		auto CurrentRenderFrameContextPtr = UseAsyncComputeQueue ? RenderFrameContextAsyncPtr : RenderFrameContextPtr;

        auto RT = CurrentRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr;
		auto AtmosphericTexture = AtmosphericShadowing->GetTexture();
        const int32 Width = RT->Info.Width;
        const int32 Height = RT->Info.Height;

        struct CommonComputeUniformBuffer
        {
            int32 Width;
            int32 Height;
            int32 Paading0;
            int32 Padding1;
        };
        CommonComputeUniformBuffer CommonComputeData;
        CommonComputeData.Width = Width;
        CommonComputeData.Height = Height;

        auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
            jNameStatic("AtmosphericShadowingApplyOneFrameUniformBuffer"), jLifeTimeType::OneFrame, sizeof(CommonComputeData)));
        OneFrameUniformBuffer->UpdateBufferData(&CommonComputeData, sizeof(CommonComputeData));

        jRHIUtil::DispatchCompute(CurrentRenderFrameContextPtr, CurrentRenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr->GetTexture()
            , [&](const std::shared_ptr<jRenderFrameContext>& RenderFrameContextPtr, jShaderBindingArray& InOutShaderBindingArray, jShaderBindingResourceInlineAllocator& InOutResourceInlineAllactor)
            {
				if (UseAsyncCompute)
				{
					if (IsUseVulkan())
					{
						g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RT->GetTexture(), EResourceLayout::UAV);
						g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericTexture, EResourceLayout::SHADER_READ_ONLY);
					}
				}
				else
				{
					// Todo : Invalid flag for async compute queue
					g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RT->GetTexture(), EResourceLayout::UAV);
					g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericTexture, EResourceLayout::SHADER_READ_ONLY);
				}

                const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                    , ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
                    , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

                InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
                    , InOutResourceInlineAllactor.Alloc<jTextureResource>(AtmosphericTexture, SamplerState)));

                InOutShaderBindingArray.Add(jShaderBinding::Create(InOutShaderBindingArray.NumOfData, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
                    , InOutResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
            }
            , [](const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
                {
                    jShaderInfo shaderInfo;
                    shaderInfo.SetName(jNameStatic("AtmosphericShadowingApplyCS"));
                    shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AtmosphericShadowingApply_cs.hlsl"));
                    shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
                    jShader* Shader = g_rhi->CreateShader(shaderInfo);
                    return Shader;
                }
            );

        if (UseAsyncCompute && UseAsyncComputeQueue)
        {
            auto ComputeSyncAcrossCommandQueuePtr = RenderFrameContextAsyncPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, false);
            ComputeSyncAcrossCommandQueuePtr->WaitSyncAcrossCommandQueue(ECommandBufferType::GRAPHICS);
        }
    }
    else
	{
        if (UseAsyncCompute)
        {
            auto ComputeSyncAcrossCommandQueuePtr = RenderFrameContextAsyncPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, false);
            ComputeSyncAcrossCommandQueuePtr->WaitSyncAcrossCommandQueue(ECommandBufferType::GRAPHICS);
        }

		std::shared_ptr<jRenderTarget> AtmosphericShadowing = RenderFrameContextPtr->SceneRenderTargetPtr->AtmosphericShadowing;

		auto RT = RenderFrameContextPtr->SceneRenderTargetPtr->ColorPtr;
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RT->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), AtmosphericShadowing->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

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
		auto BlendingState = TBlendingStateInfo<true, EBlendFactor::ONE, EBlendFactor::ONE, EBlendOp::ADD, EBlendFactor::ZERO, EBlendFactor::ONE, EBlendOp::ADD, EColorMask::ALL>::Create();

		const int32 RTWidth = RT->Info.Width;
		const int32 RTHeight = RT->Info.Height;

		// Create fixed pipeline states
		jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
			, jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

		const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
		const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

		jRenderPassInfo renderPassInfo;
		jAttachment color = jAttachment(RT, EAttachmentLoadStoreOp::LOAD_STORE
			, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, RT->GetLayout(), EResourceLayout::COLOR_ATTACHMENT);
		renderPassInfo.Attachments.push_back(color);

		jSubpass subpass;
		subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
		subpass.OutputColorAttachments.push_back(0);
		renderPassInfo.Subpasses.push_back(subpass);

		auto RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

		int32 BindingPoint = 0;
		jShaderBindingArray ShaderBindingArray;
		jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

		const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
			, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
			, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

		ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::FRAGMENT
			, ResourceInlineAllactor.Alloc<jTextureResource>(AtmosphericShadowing->GetTexture(), SamplerState)));

		RenderFrameContextPtr->GetActiveCommandBuffer()->FlushBarrierBatch();
		std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
		jShaderBindingInstanceArray ShaderBindingInstanceArray;
		ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());

		jGraphicsPipelineShader Shader;
		{
			jShaderInfo shaderInfo;
			shaderInfo.SetName(jNameStatic("AtmosphericShadowingApplyVS"));
			shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"));
			shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
			Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

			shaderInfo.SetName(jNameStatic("AtmosphericShadowingApplyPS"));
			shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/AtmosphericShadowingApply_ps.hlsl"));
			shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
			Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
		}

		if (!jSceneRenderTarget::GlobalFullscreenPrimitive)
			jSceneRenderTarget::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);
		jDrawCommand DrawCommand(RenderFrameContextPtr, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
			, Shader, &PostProcessPassPipelineStateFixed, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
		DrawCommand.Test = true;
		DrawCommand.PrepareToDraw(false);

		if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
		{
			DrawCommand.Draw();
			RenderPass->EndRenderPass();
		}
	}

	RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, false);
}

