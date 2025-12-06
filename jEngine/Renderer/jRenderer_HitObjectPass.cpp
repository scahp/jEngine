#include "pch.h"
#include "jRenderer.h"
#include "jSceneRenderTargets.h"
#include "Scene/jObject.h"
#include "Scene/jRenderObject.h"
#include "Scene/jCamera.h"
#include "Shader/jShader.h"
#include "RHI/jRHIUtil.h"
#include "jOptions.h"

#ifdef ENABLE_EDITOR_FEATURES
#include "Code/Engine/jEditor.h"
#endif

// Include DX12 API for readback
// Note: These are always included to support IsUseDX12() runtime check
#include "RHI/DX12/jRHI_DX12.h"
#include "RHI/DX12/jBuffer_DX12.h"
#include "RHI/DX12/jTexture_DX12.h"
#include "RHI/DX12/jBufferUtil_DX12.h"
#include "RHI/DX12/jCommandBufferManager_DX12.h"

void jRenderer::RequestObjectPick(int32 mouseX, int32 mouseY)
{
	bObjectPickRequested = true;
	PickMouseX = mouseX;
	PickMouseY = mouseY;
}

void jRenderer::HitObjectPass()
{
	// Check for pick requests from PlacementTool
#ifdef ENABLE_EDITOR_FEATURES
	if (g_Editor && g_Editor->Placement.bPickRequested)
	{
		bObjectPickRequested = true;
		PickMouseX = g_Editor->Placement.PickMouseX;
		PickMouseY = g_Editor->Placement.PickMouseY;
		g_Editor->Placement.bPickRequested = false;  // Consume the request
	}
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
				.ResourceName = TEXT("HitObject_RT")
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
		jShaderInfo shaderInfo;

		shaderInfo.SetName(jNameStatic("HitObject_vs"));
		shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/HitObject_vs.hlsl"));
		shaderInfo.SetShaderType(EShaderAccessStageFlag::VERTEX);
		HitObjectShader.VertexShader = g_rhi->CreateShader(shaderInfo);

		shaderInfo.SetName(jNameStatic("HitObject_ps"));
		shaderInfo.SetShaderFilepath(jNameStatic("Resource/Shaders/hlsl/HitObject_ps.hlsl"));
		shaderInfo.SetShaderType(EShaderAccessStageFlag::FRAGMENT);
		HitObjectShader.PixelShader = g_rhi->CreateShader(shaderInfo);

		// 5. Transition layouts
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), jSceneRenderTarget::HitObject_RT->GetTexture(), EResourceLayout::COLOR_ATTACHMENT);
		g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer(), RenderFrameContextPtr->SceneRenderTargetPtr->DepthPtr->GetTexture(), EResourceLayout::DEPTH_STENCIL_READ_ONLY);

		// 6. Create draw commands for all static render objects
		std::vector<jDrawCommand> HitObjectDrawCommands;
		HitObjectDrawCommands.resize(jObject::GetStaticRenderObject().size());

		int32 i = 0;
		for (auto iter : jObject::GetStaticRenderObject())
		{
			new (&HitObjectDrawCommands[i]) jDrawCommand(RenderFrameContextPtr, &View, iter, HitObjectRenderPass
				, HitObjectShader, &HitObjectPipelineStateFixed, nullptr, {}, nullptr);
			HitObjectDrawCommands[i].PrepareToDraw(false);
			++i;
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
	if (IsUseDX12())
	{
		ReadbackHitObjectDX12();
	}
	else
	{
		// Vulkan implementation - TODO
		// Placeholder: Always deselect for now
	#ifdef ENABLE_EDITOR_FEATURES
		if (g_Editor)
		{
			g_Editor->Placement.SelectObject((jRenderObject*)nullptr);
		}
	#endif
	}
}

void jRenderer::ReadbackHitObjectDX12()
{
	// Transition HitObject_RT to TRANSFER_SRC for copy
	g_rhi->TransitionLayout(RenderFrameContextPtr->GetActiveCommandBuffer()
		, jSceneRenderTarget::HitObject_RT->GetTexture(), EResourceLayout::TRANSFER_SRC);

	// Create readback buffer (4 bytes for RGBA8)
	static std::shared_ptr<jBuffer_DX12> ReadbackBuffer;
	const uint64 ReadbackBufferSize = 4;  // RGBA8 = 4 bytes

	if (!ReadbackBuffer)
	{
		ReadbackBuffer = std::shared_ptr<jBuffer_DX12>(
			jBufferUtil_DX12::CreateBuffer(ReadbackBufferSize, 0
				, EBufferCreateFlag::Readback
				, EResourceLayout::TRANSFER_DST
				, nullptr
				, ReadbackBufferSize
				, TEXT("HitObject_ReadbackBuffer")));
	}

	// Record copy to readback buffer
	auto commandBuffer_DX12 = (jCommandBuffer_DX12*)RenderFrameContextPtr->GetActiveCommandBuffer();
	auto hitObjectTexture_DX12 = (jTexture_DX12*)jSceneRenderTarget::HitObject_RT->GetTexture();

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	footprint.Offset = 0;
	footprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	footprint.Footprint.Width = 1;
	footprint.Footprint.Height = 1;
	footprint.Footprint.Depth = 1;
	footprint.Footprint.RowPitch = 256;  // Must be aligned to 256

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = (ID3D12Resource*)ReadbackBuffer->GetHandle();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	dst.PlacedFootprint = footprint;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = (ID3D12Resource*)hitObjectTexture_DX12->GetHandle();
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;

	D3D12_BOX srcBox = {};
	srcBox.left = PickMouseX;
	srcBox.top = PickMouseY;
	srcBox.right = PickMouseX + 1;
	srcBox.bottom = PickMouseY + 1;
	srcBox.front = 0;
	srcBox.back = 1;

	commandBuffer_DX12->CommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);

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
