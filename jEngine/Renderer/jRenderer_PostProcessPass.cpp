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
#include "Shader/jShader.h"
#include "Shader/jShaderParameterSet.h"

BEGIN_SHADER_PARAMETER_SET(jTonemapPSParameters)
    SHADER_TEXTURE2D(BloomTexture)
    SHADER_TEXTURE2D(Texture)
    SHADER_TEXTURE2D(EyeAdaptationTexture)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jBloomSetupPSParameters)
    SHADER_TEXTURE2D(Texture)
    SHADER_TEXTURE2D(PreEyeAdaptionTexture)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jBloomUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, BufferSizeAndInvSize)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, TintA)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector4, TintB)
    SHADER_UNIFORM_BUFFER_MEMBER(float, BloomIntensity)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jBloomDownPSParameters)
    SHADER_UNIFORM_BUFFER(jBloomUniformBuffer, BloomParam)
    SHADER_TEXTURE2D(Texture)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jBloomUpPSParameters)
    SHADER_UNIFORM_BUFFER(jBloomUniformBuffer, BloomParam)
    SHADER_TEXTURE2D(Texture)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jEyeAdaptationUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, ViewportMin)
    SHADER_UNIFORM_BUFFER_MEMBER(Vector2, ViewportMax)
    SHADER_UNIFORM_BUFFER_MEMBER(float, MinLuminanceAverage)
    SHADER_UNIFORM_BUFFER_MEMBER(float, MaxLuminanceAverage)
    SHADER_UNIFORM_BUFFER_MEMBER(float, DeltaFrametime)
    SHADER_UNIFORM_BUFFER_MEMBER(float, AdaptationSpeed)
    SHADER_UNIFORM_BUFFER_MEMBER(float, ExposureCompensation)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jEyeAdaptationCSParameters)
    SHADER_TEXTURE2D(SceneColor)
    SHADER_TEXTURE2D(EyeAdaptationTexture)
    SHADER_RW_TEXTURE2D(RWEyeAdaptationTexture)
    SHADER_UNIFORM_BUFFER(jEyeAdaptationUniformBuffer, EyeAdaptation)
END_SHADER_PARAMETER_SET()

struct jShaderBloomSetupPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jBloomSetupPSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderBloomSetupPixelShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderBloomSetupPixelShader
    , "BloomSetupPS"
    , "Resource/Shaders/hlsl/bloom_and_eyeadaptation_setup_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)

struct jShaderBloomDownVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jBloomDownPSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderBloomDownVertexShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderBloomDownVertexShader
    , "BloomDownVS"
    , "Resource/Shaders/hlsl/bloom_down_vs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::VERTEX)

struct jShaderBloomDownPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jBloomDownPSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderBloomDownPixelShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderBloomDownPixelShader
    , "BloomDownPS"
    , "Resource/Shaders/hlsl/bloom_down_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)

struct jShaderBloomUpVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jBloomUpPSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderBloomUpVertexShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderBloomUpVertexShader
    , "BloomUpVS"
    , "Resource/Shaders/hlsl/bloom_up_vs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::VERTEX)

struct jShaderBloomUpPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jBloomUpPSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderBloomUpPixelShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderBloomUpPixelShader
    , "BloomUpPS"
    , "Resource/Shaders/hlsl/bloom_up_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)

struct jShaderTonemapPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jTonemapPSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderTonemapPixelShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderTonemapPixelShader
    , "TonemapPS"
    , "Resource/Shaders/hlsl/tonemap_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)

struct jShaderEyeAdaptationComputeShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(jEyeAdaptationCSParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderEyeAdaptationComputeShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderEyeAdaptationComputeShader
    , "EyeAdaptationCS"
    , "Resource/Shaders/hlsl/eyeadaptation_cs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::COMPUTE)

void jRenderer::PostProcess()
{
	auto AddShaderParameterFullscreenPass = [&](const char* InDebugName, const std::shared_ptr<jRenderTarget> InRenderTargetPtr
		, jShader* InVertexShader, jShader* InPixelShader, const auto& InParameters)
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
			jAttachment color = {
				.RenderTargetPtr = InRenderTargetPtr,
				.LoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_STORE,
				.StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
				.RTClearValue = ClearColor,
				.InitialLayout = InRenderTargetPtr->GetLayout(),
				.FinalLayout = EResourceLayout::COLOR_ATTACHMENT
			};
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

			jShaderBindingInstanceArray ShaderBindingInstanceArray;
			auto ShaderBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(InParameters, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::SingleFrame);
			ShaderBindingInstanceArray.Add(ShaderBindingInstance.get());

			jGraphicsPipelineShader Shader;
			{
				Shader.VertexShader = InVertexShader;
				Shader.PixelShader = InPixelShader;
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

	auto AddShaderParameterComputePass = [&](const char* InDebugName, jShader* InComputeShader, const auto& InParameters
		, uint32 NumGroupsX, uint32 NumGroupsY, uint32 NumGroupsZ)
		{
			DEBUG_EVENT(RenderFrameContextPtr, InDebugName);

			auto CurrentBindingInstance = jShaderParameterSet::CreateShaderBindingInstance(InParameters, EShaderAccessStageFlag::COMPUTE, jShaderBindingInstanceType::SingleFrame);

			jShaderBindingLayoutArray ShaderBindingLayoutArray;
			ShaderBindingLayoutArray.Add(CurrentBindingInstance->ShaderBindingsLayouts);

			jPipelineStateInfo* ComputePipelineStateInfo = g_rhi->CreateComputePipelineStateInfo(InComputeShader, ShaderBindingLayoutArray, {});
			ComputePipelineStateInfo->Bind(RenderFrameContextPtr);

			jShaderBindingInstanceArray ShaderBindingInstanceArray;
			ShaderBindingInstanceArray.Add(CurrentBindingInstance.get());

			jShaderBindingInstanceCombiner ShaderBindingInstanceCombiner;
			for (int32 i = 0; i < ShaderBindingInstanceArray.NumOfData; ++i)
			{
				ShaderBindingInstanceCombiner.DescriptorSetHandles.Add(ShaderBindingInstanceArray[i]->GetHandle());
				const std::vector<uint32>* DynamicOffsets = ShaderBindingInstanceArray[i]->GetDynamicOffsets();
				if (DynamicOffsets && DynamicOffsets->size())
				{
					ShaderBindingInstanceCombiner.DynamicOffsets.Add((void*)DynamicOffsets->data(), (int32)DynamicOffsets->size());
				}
			}
			ShaderBindingInstanceCombiner.ShaderBindingInstanceArray = &ShaderBindingInstanceArray;

			g_rhi->BindComputeShaderBindingInstances(RenderFrameContextPtr->GetActiveCommandBuffer(), ComputePipelineStateInfo, ShaderBindingInstanceCombiner, 0);
			g_rhi->DispatchCompute(RenderFrameContextPtr, NumGroupsX, NumGroupsY, NumGroupsZ);
		};

	const jSamplerStateInfo* PostProcessSamplerState = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
		, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE, ETextureAddressMode::CLAMP_TO_EDGE
		, 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

	jShaderFullscreenQuadVertexShader::ShaderPermutation FullscreenQuadVertexShaderPermutation;
	jShader* const FullscreenQuadVertexShader = jShaderFullscreenQuadVertexShader::CreateShader(FullscreenQuadVertexShaderPermutation);
	jShaderBloomSetupPixelShader::ShaderPermutation BloomSetupPixelShaderPermutation;
	jShader* const BloomSetupPixelShader = jShaderBloomSetupPixelShader::CreateShader(BloomSetupPixelShaderPermutation);
	jShaderBloomDownVertexShader::ShaderPermutation BloomDownVertexShaderPermutation;
	jShader* const BloomDownVertexShader = jShaderBloomDownVertexShader::CreateShader(BloomDownVertexShaderPermutation);
	jShaderBloomDownPixelShader::ShaderPermutation BloomDownPixelShaderPermutation;
	jShader* const BloomDownPixelShader = jShaderBloomDownPixelShader::CreateShader(BloomDownPixelShaderPermutation);
	jShaderBloomUpVertexShader::ShaderPermutation BloomUpVertexShaderPermutation;
	jShader* const BloomUpVertexShader = jShaderBloomUpVertexShader::CreateShader(BloomUpVertexShaderPermutation);
	jShaderBloomUpPixelShader::ShaderPermutation BloomUpPixelShaderPermutation;
	jShader* const BloomUpPixelShader = jShaderBloomUpPixelShader::CreateShader(BloomUpPixelShaderPermutation);
	jShaderTonemapPixelShader::ShaderPermutation TonemapPixelShaderPermutation;
	jShader* const TonemapPixelShader = jShaderTonemapPixelShader::CreateShader(TonemapPixelShaderPermutation);
	jShaderEyeAdaptationComputeShader::ShaderPermutation EyeAdaptationComputeShaderPermutation;
	jShader* const EyeAdaptationComputeShader = jShaderEyeAdaptationComputeShader::CreateShader(EyeAdaptationComputeShaderPermutation);

	auto CreateBloomUniformBufferBlock = [&](int32 InWidth, int32 InHeight, Vector InTintA = Vector::ZeroVector, Vector InTintB = Vector::ZeroVector)
		{
			jBloomUniformBuffer UniformBufferData;
			UniformBufferData.BufferSizeAndInvSize = Vector4((float)InWidth, (float)InHeight, 1.0f / (float)InWidth, 1.0f / (float)InHeight);
			UniformBufferData.TintA = Vector4(InTintA, 0.0f);
			UniformBufferData.TintB = Vector4(InTintB, 0.0f);
			UniformBufferData.BloomIntensity = 0.675f;

			auto UniformBufferBlock = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
				jNameStatic("BloomUniformBuffer"), jLifeTimeType::OneFrame, sizeof(UniformBufferData)));
			UniformBufferBlock->UpdateBufferData(&UniformBufferData, sizeof(UniformBufferData));
			return UniformBufferBlock;
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
				jRenderTargetInfo Info = {
					.Type = ETextureType::TEXTURE_2D,
					.Format = ETextureFormat::R16F,
					.Width = 1,
					.Height = 1,
					.LayerCount = 1,
					.IsGenerateMipmap = false,
					.SampleCount = g_rhi->GetSelectedMSAASamples(),
					.RTClearValue = jRTClearValue::Invalid,
					.TextureCreateFlag = (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV),
					.ResourceName = jNameStatic("g_EyeAdaptationARTPtr")
				};
				g_EyeAdaptationARTPtr = jRenderTargetPool::GetRenderTarget(Info);
			}
			if (!g_EyeAdaptationBRTPtr)
			{
				jRenderTargetInfo Info = {
					.Type = ETextureType::TEXTURE_2D,
					.Format = ETextureFormat::R16F,
					.Width = 1,
					.Height = 1,
					.LayerCount = 1,
					.IsGenerateMipmap = false,
					.SampleCount = g_rhi->GetSelectedMSAASamples(),
					.RTClearValue = jRTClearValue::Invalid,
					.TextureCreateFlag = (ETextureCreateFlag::RTV | ETextureCreateFlag::UAV),
					.ResourceName = jNameStatic("g_EyeAdaptationBRTPtr")
				};
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
				jBloomSetupPSParameters BloomSetupParameters;
				BloomSetupParameters.Texture = { SourceRT, PostProcessSamplerState };
				BloomSetupParameters.PreEyeAdaptionTexture = { EyeAdaptationTextureOld, PostProcessSamplerState };
				AddShaderParameterFullscreenPass(szDebugEventTemp, SceneRT->BloomSetup
					, FullscreenQuadVertexShader, BloomSetupPixelShader
					, BloomSetupParameters);
				SourceRT = SceneRT->BloomSetup->GetTexture();

				g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);

				for (int32 i = 0; i < _countof(SceneRT->DownSample); ++i)
				{
					const auto& RTInfo = SceneRT->DownSample[i]->Info;
					sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "BloomDownsample %dx%d", RTInfo.Width, RTInfo.Height);
					jBloomDownPSParameters BloomDownParameters;
					BloomDownParameters.BloomParam.Buffer = CreateBloomUniformBufferBlock(RTInfo.Width, RTInfo.Height);
					BloomDownParameters.Texture = { SourceRT, PostProcessSamplerState };
					AddShaderParameterFullscreenPass(szDebugEventTemp, SceneRT->DownSample[i]
						, BloomDownVertexShader, BloomDownPixelShader
						, BloomDownParameters);
					SourceRT = SceneRT->DownSample[i]->GetTexture();

					g_rhi->TransitionLayout(CommandBuffer, SourceRT, EResourceLayout::SHADER_READ_ONLY);
				}
			}

			// Todo make a function for each postprocess steps
			// 여기서 EyeAdaptation 계산하는 Compute shader 추가
			if (1)
			{
				sprintf_s(szDebugEventTemp, sizeof(szDebugEventTemp), "EyeAdaptationCS %dx%d", EyeAdaptationTextureCurrent->Width, EyeAdaptationTextureCurrent->Height);

				g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SourceRT, EResourceLayout::SHADER_READ_ONLY);
				g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), EyeAdaptationTextureCurrent, EResourceLayout::UAV);

				jEyeAdaptationUniformBuffer EyeAdaptationUniformBuffer;
				EyeAdaptationUniformBuffer.ViewportMin = Vector2(0.0f, 0.0f);
				EyeAdaptationUniformBuffer.ViewportMax = Vector2((float)SourceRT->Width, (float)SourceRT->Height);
				EyeAdaptationUniformBuffer.MinLuminanceAverage = 0.03f;
				EyeAdaptationUniformBuffer.MaxLuminanceAverage = 8.0f;
				EyeAdaptationUniformBuffer.DeltaFrametime = 1.0f / 60.0f;
				EyeAdaptationUniformBuffer.AdaptationSpeed = 1.0f;
				EyeAdaptationUniformBuffer.ExposureCompensation = exp2(gOptions.AutoExposureKeyValueScale);

				auto EyeAdaptationUniformBufferBlock = std::shared_ptr<IUniformBufferBlock>(g_rhi->CreateUniformBufferBlock(
					jNameStatic("EyeAdaptationUniformBuffer"), jLifeTimeType::OneFrame, sizeof(EyeAdaptationUniformBuffer)));
				EyeAdaptationUniformBufferBlock->UpdateBufferData(&EyeAdaptationUniformBuffer, sizeof(EyeAdaptationUniformBuffer));

				jEyeAdaptationCSParameters EyeAdaptationParameters;
				EyeAdaptationParameters.SceneColor = { SourceRT, nullptr };
				EyeAdaptationParameters.EyeAdaptationTexture = { EyeAdaptationTextureOld, nullptr };
				EyeAdaptationParameters.RWEyeAdaptationTexture = { EyeAdaptationTextureCurrent };
				EyeAdaptationParameters.EyeAdaptation.Buffer = EyeAdaptationUniformBufferBlock;
				AddShaderParameterComputePass(szDebugEventTemp, EyeAdaptationComputeShader, EyeAdaptationParameters, 1, 1, 1);
			}

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
					jBloomUpPSParameters BloomUpParameters;
					BloomUpParameters.BloomParam.Buffer = CreateBloomUniformBufferBlock(RTInfo.Width, RTInfo.Height, UpscaleBloomTintA[i], UpscaleBloomTintB[i]);
					BloomUpParameters.Texture = { SourceRT, PostProcessSamplerState };
					AddShaderParameterFullscreenPass(szDebugEventTemp, SceneRT->UpSample[i]
						, BloomUpVertexShader, BloomUpPixelShader
						, BloomUpParameters);
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
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SourceRT, EResourceLayout::SHADER_READ_ONLY);
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), SceneRT->ColorPtr->GetTexture(), EResourceLayout::SHADER_READ_ONLY);
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), EyeAdaptationTextureCurrent, EResourceLayout::SHADER_READ_ONLY);

		jTonemapPSParameters TonemapParameters;
		TonemapParameters.BloomTexture = { SourceRT, PostProcessSamplerState };
		TonemapParameters.Texture = { SceneRT->ColorPtr->GetTexture(), PostProcessSamplerState };
		TonemapParameters.EyeAdaptationTexture = { EyeAdaptationTextureCurrent, PostProcessSamplerState };
		AddShaderParameterFullscreenPass(szDebugEventTemp, SceneRT->FinalColorPtr
			, FullscreenQuadVertexShader, TonemapPixelShader
			, TonemapParameters);
	}
}
