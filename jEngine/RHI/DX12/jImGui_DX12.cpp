#include "pch.h"
#include "jImGui_DX12.h"
#include "jRHI_DX12.h"
#include "jOptions.h"
#include "jTexture_DX12.h"

jImGUI_DX12::jImGUI_DX12()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
}

jImGUI_DX12::~jImGUI_DX12()
{
    Release();
}

void jImGUI_DX12::Initialize(float width, float height)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 4096;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (JFAIL(g_rhi_dx12->Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imgui_SrvDescHeap))))
        return;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    check(g_rhi_dx12->m_hWnd);
    auto& io = ImGui::GetIO();
    const ImGuiConfigFlags saved = io.ConfigFlags;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui_ImplWin32_Init(g_rhi_dx12->m_hWnd);

    ImGui_ImplDX12_Init(g_rhi_dx12->Device.Get(), g_rhi_dx12->MaxFrameCount,
        DXGI_FORMAT_R8G8B8A8_UNORM, m_imgui_SrvDescHeap.Get(),
        m_imgui_SrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
        m_imgui_SrvDescHeap->GetGPUDescriptorHandleForHeapStart());
    ImGui_ImplDX12_SetPlatformCommandQueue(g_rhi_dx12->GraphicsCommandBufferManager->GetCommandQueue().Get());
    io.ConfigFlags = saved;

    IsInitialized = true;
}

void jImGUI_DX12::Release()
{
    // Cleanup
    if (m_imgui_SrvDescHeap)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        m_imgui_SrvDescHeap.Reset();
        RegisteredTextures.clear();
        NextTextureDescriptor = 1;
        FreeTextureDescriptors.clear();
        IsInitialized = false;
    }
}

void* jImGUI_DX12::RegisterTexture(const std::shared_ptr<jTexture>& texture)
{
    if (!IsInitialized || !texture || (FreeTextureDescriptors.empty() && NextTextureDescriptor >= 4096)) return nullptr;
    auto* native = static_cast<jTexture_DX12*>(texture.get());
    if (!native->SRV.IsValid()) return nullptr;
    const UINT stride = g_rhi_dx12->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu = m_imgui_SrvDescHeap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = m_imgui_SrvDescHeap->GetGPUDescriptorHandleForHeapStart();
    const uint32 slot = FreeTextureDescriptors.empty() ? NextTextureDescriptor++ : FreeTextureDescriptors.back();
    if (!FreeTextureDescriptors.empty()) FreeTextureDescriptors.pop_back();
    cpu.ptr += SIZE_T(slot) * stride;
    gpu.ptr += UINT64(slot) * stride;
    g_rhi_dx12->Device->CopyDescriptorsSimple(1, cpu, native->SRV.CPUHandle,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (RegisteredTextures.size() <= slot) RegisteredTextures.resize(slot + 1);
    RegisteredTextures[slot] = texture;
    return reinterpret_cast<void*>(gpu.ptr);
}
void jImGUI_DX12::UnregisterTexture(void* id)
{
    if (!m_imgui_SrvDescHeap || !id) return;
    const auto start = m_imgui_SrvDescHeap->GetGPUDescriptorHandleForHeapStart().ptr;
    const auto handle = reinterpret_cast<uintptr_t>(id);
    const UINT stride = g_rhi_dx12->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (handle < start || (handle - start) % stride) return;
    const uint64 slot = (handle - start) / stride;
    if (!slot || slot >= RegisteredTextures.size() || !RegisteredTextures[slot]) return;
    RegisteredTextures[slot].reset(); FreeTextureDescriptors.push_back(uint32(slot));
}

void jImGUI_DX12::Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContextPtr)
{
    if (!IsInitialized)
        return;

    jRenderPass* UIRenderPass = nullptr;
    jRasterizationStateInfo* RasterizationState = TRasterizationStateInfo<EPolygonMode::FILL, ECullMode::BACK, EFrontFace::CCW, false, 0.0f, 0.0f, 0.0f, 1.0f, false, false, (EMSAASamples)1, true, 0.2f, false, false>::Create();;
    auto DepthStencilState = TDepthStencilStateInfo<false, false, ECompareOp::LESS, false, false, 0.0f, 1.0f>::Create();
    auto BlendingState = TBlendingStateInfo<false, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();

    jPipelineStateFixedInfo BasePassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, BlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    auto TranslucentBlendingState = TBlendingStateInfo<true, EBlendFactor::SRC_ALPHA, EBlendFactor::ONE_MINUS_SRC_ALPHA, EBlendOp::ADD, EBlendFactor::ONE, EBlendFactor::ZERO, EBlendOp::ADD, EColorMask::ALL>::Create();
    jPipelineStateFixedInfo TranslucentPassPipelineStateFixed = jPipelineStateFixedInfo(RasterizationState, DepthStencilState, TranslucentBlendingState
        , jViewport(0.0f, 0.0f, (float)SCR_WIDTH, (float)SCR_HEIGHT), jScissor(0, 0, SCR_WIDTH, SCR_HEIGHT), gOptions.UseVRS);

    const jRTClearValue ClearColor = jRTClearValue(0.0f, 0.0f, 0.0f, 1.0f);
    const jRTClearValue ClearDepth = jRTClearValue(1.0f, 0);

    // Setup attachment
    jRenderPassInfo renderPassInfo;
    const jSwapchainImage* image = g_rhi_dx12->Swapchain->GetCurrentSwapchainImage();
	g_rhi->TransitionLayout(InRenderFrameContextPtr->GetActiveCommandBuffer(), image->TexturePtr.get(), EResourceLayout::COLOR_ATTACHMENT);

    auto BackBuffer = std::make_shared<jRenderTarget>(image->TexturePtr);
    jAttachment color = {
        .RenderTargetPtr = BackBuffer,
        .LoadStoreOp = EAttachmentLoadStoreOp::LOAD_STORE,
        .StencilLoadStoreOp = EAttachmentLoadStoreOp::DONTCARE_DONTCARE,
        .RTClearValue = ClearColor,
        .InitialLayout = BackBuffer->GetLayout(),
        .FinalLayout = EResourceLayout::COLOR_ATTACHMENT
    };
    renderPassInfo.Attachments.push_back(color);
    UIRenderPass = (jRenderPass_Vulkan*)g_rhi->GetOrCreateRenderPass(renderPassInfo, { 0, 0 }, { SCR_WIDTH, SCR_HEIGHT });

    DEBUG_EVENT_WITH_COLOR(InRenderFrameContextPtr, "ImGUI", Vector4(0.8f, 0.8f, 0.8f, 1.0f));

    if (UIRenderPass && UIRenderPass->BeginRenderPass(InRenderFrameContextPtr->GetActiveCommandBuffer()))
    {
        //// Copy ColorRT to BackBuffer
        //CopyDrawCommand.Draw();

        // Draw UI to BackBuffer Directly
        const jCommandBuffer_DX12* commandBuffer = (const jCommandBuffer_DX12*)InRenderFrameContextPtr->GetActiveCommandBuffer();
        ID3D12DescriptorHeap* DescriptorHeap = m_imgui_SrvDescHeap.Get();
        commandBuffer->CommandList->SetDescriptorHeaps(1, &DescriptorHeap);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandBuffer->CommandList.Get());

        UIRenderPass->EndRenderPass();
    }
}

void jImGUI_DX12::NewFrameInternal()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}
