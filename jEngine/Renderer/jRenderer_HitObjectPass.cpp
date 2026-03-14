#include "pch.h"
#include "jRenderer.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "Shader/jCommonShaderParameters.h"
#include "Shader/jShader.h"
#include "RHI/jRHIUtil.h"
#include "jOptions.h"

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#endif

namespace
{
struct jShaderHitObjectVertexShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderHitObjectVertexShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHitObjectVertexShader
    , "HitObject_vs"
    , "Resource/Shaders/hlsl/HitObject_vs.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::VERTEX)

struct jShaderHitObjectPixelShader : public jShader
{
    DECLARE_SHADER_PARAMETER_SETS(
        jViewShaderParameters,
        jRenderObjectShaderParameters)

    using ShaderPermutation = jPermutation<>;
    ShaderPermutation Permutation;

    DECLARE_SHADER_WITH_PERMUTATION(jShaderHitObjectPixelShader, Permutation)
};

IMPLEMENT_SHADER_WITH_PERMUTATION(jShaderHitObjectPixelShader
    , "HitObject_ps"
    , "Resource/Shaders/hlsl/HitObject_ps.hlsl"
    , ""
    , "main"
    , EShaderAccessStageFlag::FRAGMENT)
}

void jRenderer::RequestObjectPick(int32 mouseX, int32 mouseY)
{
	bObjectPickRequested = true;
	PickMouseX = mouseX;
	PickMouseY = mouseY;
}

void jRenderer::HitObjectPass()
{
	// HitObject picking is editor-only and should run only in Placement Mode.
#ifdef ENABLE_EDITOR_FEATURES
	if (!g_Editor || !g_Editor->Placement.EnablePlacementMode)
	{
		bObjectPickRequested = false;
		if (g_Editor)
		{
			g_Editor->Placement.bPickRequested = false;
		}
		return;
	}

	// Check for pick requests from PlacementTool
	if (g_Editor->Placement.bPickRequested)
	{
		bObjectPickRequested = true;
		PickMouseX = g_Editor->Placement.PickMouseX;
		PickMouseY = g_Editor->Placement.PickMouseY;
		g_Editor->Placement.bPickRequested = false;  // Consume the request
	}
#else
	return;
#endif

	if (!bObjectPickRequested)
		return;

	bObjectPickRequested = false;

	{
		SCOPE_CPU_PROFILE(HitObjectPass);
		SCOPE_GPU_PROFILE(RenderFrameContextPtr, HitObjectPass);
		DEBUG_EVENT(RenderFrameContextPtr, "HitObjectPass");

		// 1. Create HitObject render target (once)
		if (!jSceneRenderTarget::HitObject_RT ||
			jSceneRenderTarget::HitObject_RT->Info.Width != SCR_WIDTH ||
			jSceneRenderTarget::HitObject_RT->Info.Height != SCR_HEIGHT)
		{
			jRenderTargetInfo HitObjectRTInfo = {
				.Type = ETextureType::TEXTURE_2D,
				.Format = ETextureFormat::RGBA8,
				.Width = SCR_WIDTH,
				.Height = SCR_HEIGHT,
				.LayerCount = 1,
				.IsGenerateMipmap = false,
				.SampleCount = EMSAASamples::COUNT_1,
				.RTClearValue = jRTClearValue(0.0f, 0.0f, 0.0f, 0.0f),
				.TextureCreateFlag = ETextureCreateFlag::RTV,
				.ResourceName = jNameStatic("HitObject_RT")
			};
			jSceneRenderTarget::HitObject_RT = g_rhi->CreateRenderTarget(HitObjectRTInfo);
		}

		// 2. Setup render pass
		const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 0.0f);
		const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

		jAttachment colorAttachment = {
			.RenderTargetPtr = jSceneRenderTarget::HitObject_RT,
			.LoadStoreOp = EAttachmentLoadStoreOp::CLEAR_STORE,
			.StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
			.RTClearValue = ClearColor,
			.InitialLayout = EResourceLayout::UNDEFINED,
			.FinalLayout = EResourceLayout::COLOR_ATTACHMENT
		};

		jAttachment depthAttachment = {
			.RenderTargetPtr = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr,
			.LoadStoreOp = EAttachmentLoadStoreOp::LOAD_DONTCARE,
			.StencilLoadStoreOp = EAttachmentLoadStoreOp::LOAD_DONTCARE,
			.RTClearValue = ClearDepth,
			.InitialLayout = RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetLayout(),
			.FinalLayout = EResourceLayout::DEPTH_STENCIL_READ_ONLY
		};

		jRenderPassInfo renderPassInfo;
		renderPassInfo.Attachments.push_back(colorAttachment);
		const int32 DepthAttachmentIndex = (int32)renderPassInfo.Attachments.size();
		renderPassInfo.Attachments.push_back(depthAttachment);

		// Setup subpass
		jSubpass subpass;
		subpass.Initialize(0, 1, EPipelineStageMask::COLOR_ATTACHMENT_OUTPUT_BIT, EPipelineStageMask::FRAGMENT_SHADER_BIT);
		subpass.OutputColorAttachments.push_back(0);
		subpass.OutputDepthAttachment = DepthAttachmentIndex;
		subpass.DepthAttachmentReadOnly = true;
		renderPassInfo.Subpasses.push_back(subpass);

		jRenderPass* HitObjectRenderPass = g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

		// 3. Setup pipeline state
		jRasterizationStateInfo* RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();
		auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
		auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

		jPipelineStateFixedInfo HitObjectPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
			, jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), false);

		// 4. Create shaders
		jGraphicsPipelineShader HitObjectShader;
		HitObjectShader.VertexShader = jShaderHitObjectVertexShader::CreateShader(jShaderHitObjectVertexShader::ShaderPermutation());
		HitObjectShader.PixelShader = jShaderHitObjectPixelShader::CreateShader(jShaderHitObjectPixelShader::ShaderPermutation());

		// 5. Transition layouts
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::HitObject_RT->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::DEPTH_STENCIL_READ_ONLY);

		// 6. Create draw commands for static render objects and pickable debug billboards.
		int32 NumHitObjectRenderObjects = static_cast<int32>(jObject::GetStaticRenderObject().size());
		for (auto* debugObject : jObject::GetDebugObject())
		{
			NumHitObjectRenderObjects += static_cast<int32>(debugObject->RenderObjects.size());
		}

		std::vector<jDrawCommand> HitObjectDrawCommands;
		HitObjectDrawCommands.resize(NumHitObjectRenderObjects);

		int32 i = 0;
		for (auto iter : jObject::GetStaticRenderObject())
		{
			new (&HitObjectDrawCommands[i]) jDrawCommand(RenderFrameContextPtr, &View, iter, HitObjectRenderPass
				, HitObjectShader, &HitObjectPipelineStateFixed, nullptr, {}, nullptr);
			HitObjectDrawCommands[i].PrepareToDraw(false);
			++i;
		}
		for (auto* debugObject : jObject::GetDebugObject())
		{
			for (auto* renderObject : debugObject->RenderObjects)
			{
				new (&HitObjectDrawCommands[i]) jDrawCommand(RenderFrameContextPtr, &View, renderObject, HitObjectRenderPass
					, HitObjectShader, &HitObjectPipelineStateFixed, nullptr, {}, nullptr);
				HitObjectDrawCommands[i].PrepareToDraw(false);
				++i;
			}
		}

		// 7. Begin render pass and draw objects
		if (HitObjectRenderPass && HitObjectRenderPass->BeginRenderPass(RenderFrameContextPtr->GetActiveCommandBuffer()))
		{
			for (const auto& command : HitObjectDrawCommands)
			{
				command.Draw();
			}

			HitObjectRenderPass->EndRenderPass();
		}
	}

	// 8. Readback pixel at (PickMouseX, PickMouseY) and decode ObjectID
	ReadbackHitObject();
}

void jRenderer::ReadbackHitObject()
{
	static constexpr uint32 HitObjectReadbackRowPitch = 256;
	static constexpr uint64 HitObjectReadbackBufferSize = HitObjectReadbackRowPitch;

	// Transition HitObject_RT to TRANSFER_SRC for copy
	g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer()
		, jSceneRenderTarget::HitObject_RT->GetTexture(), EResourceLayout::TRANSFER_SRC);

	RenderFrameContextPtr->GetActiveCommandBuffer()->FlushBarrierBatch();

	// DX12 texture-to-buffer copies require a 256-byte aligned row pitch even for a 1x1 RGBA8 region.
	static std::shared_ptr<jBuffer> ReadbackBuffer;

	if (!ReadbackBuffer || ReadbackBuffer->GetBufferSize() < HitObjectReadbackBufferSize)
	{
		ReadbackBuffer = g_rhi->CreateRawBuffer(HitObjectReadbackBufferSize, 0
			, EBufferCreateFlag::Readback
			, EResourceLayout::TRANSFER_DST
			, nullptr
			, HitObjectReadbackBufferSize
			, jNameStatic("HitObject_ReadbackBuffer"));
	}

	jTextureCopyRegion copyRegion;
	copyRegion.X = Clamp(PickMouseX, 0, Max(0, SCR_WIDTH - 1));
	copyRegion.Y = Clamp(PickMouseY, 0, Max(0, SCR_HEIGHT - 1));
	copyRegion.Width = 1;
	copyRegion.Height = 1;
	copyRegion.DestRowPitchOverride = HitObjectReadbackRowPitch;

	g_rhi->CopyTextureRegionToBuffer(RenderFrameContextPtr->GetActiveCommandBuffer()
		, jSceneRenderTarget::HitObject_RT->GetTexture()
		, copyRegion
		, ReadbackBuffer.get());

	// Submit command buffer and wait until copy is complete before mapping
	RenderFrameContextPtr->SubmitCurrentActiveCommandBuffer(jRenderFrameContext::None, true);

	// Active command buffer is refreshed after submit; begin recording for subsequent passes
	RenderFrameContextPtr->GetActiveCommandBuffer()->Begin();

	uint8* MappedPtr = (uint8*)ReadbackBuffer->Map();
	if (MappedPtr)
	{
		jRenderObjectID renderObjectID =
			(jRenderObjectID(MappedPtr[0]) << 0) |
			(jRenderObjectID(MappedPtr[1]) << 8) |
			(jRenderObjectID(MappedPtr[2]) << 16) |
			(jRenderObjectID(MappedPtr[3]) << 24);

		ReadbackBuffer->Unmap();

#ifdef ENABLE_EDITOR_FEATURES
		if (g_Editor)
		{
			if (renderObjectID != 0)
			{
				jRenderObject* picked = jRenderObject::FindRenderObjectByID(renderObjectID);
				g_Editor->Placement.SelectObject(picked);
			}
			else
			{
				g_Editor->Placement.SelectObject((jRenderObject*)nullptr);
			}
		}
#endif
	}
}
