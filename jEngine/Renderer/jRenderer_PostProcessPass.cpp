#include "pch.h"
#include "jRenderer.h"
#include "jSceneRenderTargets.h"
#include "RHI/jRenderTarget.h"
#include "jPrimitiveUtil.h"
#include "RHI/jRenderPass.h"
#include "RHI/jPipelineStateInfo.h"
#include "RHI/jRenderTargetPool.h"
#include "jDrawCommand.h"
#include "jOptions.h"
#include "Scene/jRenderObject.h"
#include "Profiler/jPerformanceProfile.h"

void jRenderer::PostProcess()
{
	auto AddFullQuadPass = [&](const char* InDebugName, const std::vector<jTexture*> InShaderInputs, const std::shared_ptr<jRenderTarget> InRenderTargetPtr
		, jName VertexShader, jName PixelShader, bool IsBloom = false, Vector InBloomTintA = Vector::ZeroVector, Vector InBloomTintB = Vector::ZeroVector)
		{
			DEBUG_EVENT(RenderFrameContextPtr, InDebugName);

			if (!jSceneRenderTarget::GlobalFullscreenPrimitive)
				jSceneRenderTarget::GlobalFullscreenPrimitive = jPrimitiveUtil::CreateFullscreenQuad(nullptr);

			g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InRenderTargetPtr->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);

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
			auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

			const int32 RTWidth = InRenderTargetPtr->Info.Width;
			const int32 RTHeight = InRenderTargetPtr->Info.Height;

			// Create fixed pipeline states
			jPipelineStateFixedInfo PostProcessPassPipelineStateFixed(RasterizationState, DepthStencilState, BlendingState
				, jViewport(0.0f, 0.0f, (float)RTWidth, (float)RTHeight), jScissor(0, 0, RTWidth, RTHeight), gOptions.UseVRS);

			const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
			const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

			jRenderPassInfo renderPassInfo;
			jAttachment color = jAttachment(InRenderTargetPtr, EAttachmentLoadStoreOp::DONTCARE_STORE
				, EAttachmentLoadStoreOp::DONTCARE_DONTCARE, ClearColor, InRenderTargetPtr->GetLayout(), EResourceLayout::COLOR_ATTACHMENT);
			renderPassInfo.Attachments.push_back(color);

			// Setup subpass of LightingPass
			jSubpass subpass;
			subpass.SourceSubpassIndex = 0;
			subpass.DestSubpassIndex = 1;
			subpass.OutputColorAttachments.push_back(0);
			subpass.AttachmentProducePipelineBit = EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT;
			renderPassInfo.Subpasses.push_back(subpass);

			// Create RenderPass
			jRenderPass* RenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { RTWidth, RTHeight });

			int32 BindingPoint = 0;
			jShaderBindingArray ShaderBindingArray;
			jShaderBindingResourceInlineAllocator ResourceInlineAllactor;
			jShaderBindingInstanceArray ShaderBindingInstanceArray;

			struct jBloomUniformBuffer
			{
				Vector4 BufferSizeAndInvSize;
				Vector4 TintA;
				Vector4 TintB;
				float BloomIntensity;
			};
			jBloomUniformBuffer ubo;
			ubo.BufferSizeAndInvSize.x = (float)RTWidth;
			ubo.BufferSizeAndInvSize.y = (float)RTHeight;
			ubo.BufferSizeAndInvSize.z = 1.0f / (float)RTWidth;
			ubo.BufferSizeAndInvSize.w = 1.0f / (float)RTHeight;
			ubo.TintA = Vector4(InBloomTintA, 0.0);
			ubo.TintB = Vector4(InBloomTintB, 0.0);
			ubo.BloomIntensity = 0.675f;

			auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(jNameStatic("BloomUniformBuffer"), jLifeTimeType::OneFrame, sizeof(ubo)));
			if (IsBloom)
			{
				OneFrameUniformBuffer->UpdateBufferData(&ubo, sizeof(ubo));
			}

			std::shared_ptr<jShaderBindingInstance> ShaderBindingInstance;
			{
				if (IsBloom)
				{
					ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
						, ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
				}

				const jSamplerStateInfo* SamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
					, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
					, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), false, ECompareOp::LESS>::Create();

				for (int32 i = 0; i < (int32)InShaderInputs.size(); ++i)
				{
					ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
						, ResourceInlineAllactor.Alloc<jTextureResource>(InShaderInputs[i], SamplerState)));

					g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), InShaderInputs[i], EResourceLayout::SHADER_READ_ONLY);
				}

				ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);
				ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());
			}

			jGraphicsPipelineShader Shader;
			{
				jShaderInfo shaderInfo;
				shaderInfo.SetName(VertexShader);
				shaderInfo.SetShaderFilepath(VertexShader);
				shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
				Shader.VertexShader = g_rhi->CreateShader(shaderInfo);

				shaderInfo.SetName(PixelShader);
				shaderInfo.SetShaderFilepath(PixelShader);
				shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
				Shader.PixelShader = g_rhi->CreateShader(shaderInfo);
			}

			jDrawCommand DrawCommand(RenderFrameContextPtr, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0], RenderPass
				, Shader, &PostProcessPassPipelineStateFixed, jSceneRenderTarget::GlobalFullscreenPrimitive->RenderObjects[0]->MaterialPtr.get(), ShaderBindingInstanceArray, nullptr);
			DrawCommand.Test = true;
			DrawCommand.PrepareToDraw(false);

			if (RenderPass && RenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
			{
				DrawCommand.Draw();
				RenderPass->EndRenderPass();
			}
		};

	SCOPE_CPU_PROFILE(PostProcess);
	if (1)
	{
		SCOPE_GPU_PROFILE(RenderFrameContextPtr, PostProcess);
		DEBUG_EVENT_WITH_COLOR(RenderFrameContextPtr, "PostProcess", Vector4(0.0f, 0.8f, 0.8f, 1.0f));

		const uint32 imageIndex = RenderFrameContextPtr->FrameIndex;
		char szDebugEventTemp[1024] = { 0, };

		jSceneRenderTarget* SceneRT = RenderFrameContextPtr->SceneRenderTargetPtr.get();
		jTexture* SourceRT = nullptr;
		jTexture* EyeAdaptationTextureCurrent = nullptr;
		if (gOptions.BloomEyeAdaptation && !gOptions.ShowAOOnly)
		{
			//////////////////////////////////////////////////////////////////////////
			// Todo remove this hardcode
			if (!g_EyeAdaptationARTPtr)
			{
				jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue::Invalid, (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV) };
				Info.ResourceName = TEXT("g_EyeAdaptationARTPtr");
				g_EyeAdaptationARTPtr = jRenderTargetPool::GetRenderTarget(Info);
			}
			if (!g_EyeAdaptationBRTPtr)
			{
				jRenderTargetInfo Info = { ETextureType::TEXTURE_2D, ETextureFormat::R16F, 1, 1, 1, false, g_rhi->GetSelectedMSAASamples(), jRTClearValue::Invalid, (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV) };
				Info.ResourceName = TEXT("g_EyeAdaptationBRTPtr");
				g_EyeAdaptationBRTPtr = jRenderTargetPool::GetRenderTarget(Info);
			}

			static bool FlipEyeAdaptation = false;
			FlipEyeAdaptation = !FlipEyeAdaptation;

			jTexture* EyeAdaptationTextureOld = FlipEyeAdaptation ? g_EyeAdaptationARTPtr->GetTexture() : g_EyeAdaptationBRTPtr->GetTexture();
			EyeAdaptationTextureCurrent = FlipEyeAdaptation ? g_EyeAdaptationBRTPtr->GetTexture() : g_EyeAdaptationARTPtr->GetTexture();

			if (1)
			{
				jCommandBuffer* CommandBuffer = RenderFrameContextPtr->GetActiveCommandBuffer();

				g_rhi->TransitionLayout(CommandBuffer, EyeAdaptationTextureOld, EResourceLayout::SHADER_READ_ONLY);
				g_rhi->TransitionLayout(CommandBuffer, SceneRT->ColorPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);

				SourceRT = SceneRT->ColorPtr->GetTexture();

				sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomEyeAdaptationSetup %dx%d", SceneRT->BloomSetup->Info.Width, SceneRT->BloomSetup->Info.Height);
				AddFullQuadPass(szDebugEventTemp, { SourceRT, EyeAdaptationTextureOld }, SceneRT->BloomSetup
					, jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_and_eyeadaptation_setup_ps.hlsl"));
				SourceRT = SceneRT->BloomSetup->GetTexture();

				g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);

				for (int32 i = 0; i < _countof(SceneRT->DownSample); ++i)
				{
					const auto& RTInfo = SceneRT->DownSample[i]->Info;
					sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomDownsample %dx%d", RTInfo.Width, RTInfo.Height);
					AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->DownSample[i]
						, jNameStatic("Resource/Shaders/hlsl/bloom_down_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_down_ps.hlsl"), true);
					SourceRT = SceneRT->DownSample[i]->GetTexture();

					g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);
				}
			}

			// Todo make a function for each postprocess steps
			// 여기서 EyeAdaptation 계산하는 Compute shader 추가
			if (1)
			{
				sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "EyeAdaptationCS %dx%d", EyeAdaptationTextureCurrent->Width, EyeAdaptationTextureCurrent->Height);
				DEBUG_EVENT(RenderFrameContextPtr, szDebugEventTemp);
				//////////////////////////////////////////////////////////////////////////
				// Compute Pipeline

				g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SourceRT, EResourceLayout::SHADER_READ_ONLY);
				g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), EyeAdaptationTextureCurrent, EResourceLayout::UAV);

				std::shared_ptr<jShaderBindingInstance> CurrentBindingInstance = nullptr;
				int32 BindingPoint = 0;
				jShaderBindingArray ShaderBindingArray;
				jShaderBindingResourceInlineAllocator ResourceInlineAllactor;

				// Binding 0 : Source Log2Average Image
				if (ensure(SourceRT))
				{
					ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
						, ResourceInlineAllactor.Alloc<jTextureResource>(SourceRT, nullptr)));
				}

				// Binding 1 : Prev frame EyeAdaptation Image
				if (ensure(EyeAdaptationTextureOld))
				{
					ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::COMPUTE
						, ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureOld, nullptr)));
				}

				// Binding 2 : Current frame EyeAdaptation Image
				if (ensure(EyeAdaptationTextureCurrent))
				{
					ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::TEXTURE_UAV, EShaderAccessStageFlag::COMPUTE
						, ResourceInlineAllactor.Alloc<jTextureResource>(EyeAdaptationTextureCurrent, nullptr)));
				}

				// Binding 3 : CommonComputeUniformBuffer
				struct jEyeAdaptationUniformBuffer
				{
					Vector2 ViewportMin;
					Vector2 ViewportMax;
					float MinLuminanceAverage;
					float MaxLuminanceAverage;
					float DeltaFrametime;
					float AdaptationSpeed;
					float ExposureCompensation;
				};
				jEyeAdaptationUniformBuffer EyeAdaptationUniformBuffer;
				EyeAdaptationUniformBuffer.ViewportMin = Vector2(0.0f, 0.0f);
				EyeAdaptationUniformBuffer.ViewportMax = Vector2((float)SourceRT->Width, (float)SourceRT->Height);
				EyeAdaptationUniformBuffer.MinLuminanceAverage = 0.03f;
				EyeAdaptationUniformBuffer.MaxLuminanceAverage = 8.0f;
				EyeAdaptationUniformBuffer.DeltaFrametime = 1.0f / 60.0f;
				EyeAdaptationUniformBuffer.AdaptationSpeed = 1.0f;
				EyeAdaptationUniformBuffer.ExposureCompensation = exp2(gOptions.AutoExposureKeyValueScale);

				auto OneFrameUniformBuffer = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
					jNameStatic("EyeAdaptationUniformBuffer"), jLifeTimeType::OneFrame, sizeof(EyeAdaptationUniformBuffer)));
				OneFrameUniformBuffer->UpdateBufferData(&EyeAdaptationUniformBuffer, sizeof(EyeAdaptationUniformBuffer));
				{
					ShaderBindingArray.Add(jShaderBinding::Create(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::COMPUTE
						, ResourceInlineAllactor.Alloc<jUniformBufferResource>(OneFrameUniformBuffer.get()), true));
				}

				CurrentBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::SingleFrame);

				jShaderInfo shaderInfo;
				shaderInfo.SetName(jNameStatic("eyeadaptation"));
				shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/eyeadaptation_cs.hlsl"));
				shaderInfo.SetShaderType(EShaderAccessStageFlag::COMPUTE);
				static jShader* Shader = g_rhi->CreateShader(shaderInfo);

				jShaderBindingLayoutArray ShaderBindingLayoutArray;
				ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

				jPipelineStateInfo* computePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(Shader, ShaderBindingLayoutArray, {});

				computePipelineStateInfo->Bind(RenderFrameContextPtr);

				//CurrentBindingInstance->BindCompute(RenderFrameContextPtr, (VkPipelineLayout)computePipelineStateInfo->GetPipelineLayoutHandle());

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

				g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), computePipelineStateInfo, ShaderBindingInstanceCombiner, 0);
				g_rhi->DispatchCompute(RenderFrameContextPtr, 1, 1, 1);
			}
			//////////////////////////////////////////////////////////////////////////

			if (1)
			{
				Vector UpscaleBloomTintA[3] = {
					Vector(0.066f, 0.066f, 0.066f) * 0.675f,
					Vector(0.1176f, 0.1176f, 0.1176f) * 0.675f,
					Vector(0.138f, 0.138f, 0.138f) * 0.675f * 0.5f
				};

				Vector UpscaleBloomTintB[3] = {
					Vector(0.066f, 0.066f, 0.066f) * 0.675f,
					Vector::OneVector,
					Vector::OneVector
				};

				auto CommandBuffer = (jCommandBuffer*)RenderFrameContextPtr->GetActiveCommandBuffer();
				for (int32 i = 0; i < _countof(SceneRT->UpSample); ++i)
				{
					const auto& RTInfo = SceneRT->UpSample[i]->Info;
					sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomUpsample %dx%d", RTInfo.Width, RTInfo.Height);
					AddFullQuadPass(szDebugEventTemp, { SourceRT }, SceneRT->UpSample[i]
						, jNameStatic("Resource/Shaders/hlsl/bloom_up_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/bloom_up_ps.hlsl"), true, UpscaleBloomTintA[i], UpscaleBloomTintB[i]);
					SourceRT = SceneRT->UpSample[i]->GetTexture();

					g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);
				}

				g_rhi->TransitionLayout(CommandBuffer, EyeAdaptationTextureCurrent, EResourceLayout::SHADER_READ_ONLY);
			}
		}
		else
		{
			SourceRT = GBlackTexture.get();
			EyeAdaptationTextureCurrent = GWhiteTexture.get();
		}
		sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "Tonemap %dx%d", SceneRT->FinalColorPtr->Info.Width, SceneRT->FinalColorPtr->Info.Height);
		AddFullQuadPass(szDebugEventTemp, { SourceRT, SceneRT->ColorPtr->GetTexture(), EyeAdaptationTextureCurrent }, SceneRT->FinalColorPtr
			, jNameStatic("Resource/Shaders/hlsl/fullscreenquad_vs.hlsl"), jNameStatic("Resource/Shaders/hlsl/tonemap_ps.hlsl"));
	}
}