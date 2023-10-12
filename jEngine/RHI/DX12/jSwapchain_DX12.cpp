#include "pch.h"
#include "jSwapchain_DX12.h"
#include "jRHIType_DX12.h"
#include "jTexture_DX12.h"
#include "jBufferUtil_DX12.h"

//////////////////////////////////////////////////////////////////////////
// jSwapchainImage_DX12
//////////////////////////////////////////////////////////////////////////
void jSwapchainImage_DX12::Release()
{
    ReleaseInternal();
}

void jSwapchainImage_DX12::ReleaseInternal()
{
    TexturePtr = nullptr;
    //if (Available)
    //{
    //    g_rhi->GetSemaphoreManager()->ReturnSemaphore(Available);
    //    Available = nullptr;
    //}
    //if (RenderFinished)
    //{
    //    g_rhi->GetSemaphoreManager()->ReturnSemaphore(RenderFinished);
    //    RenderFinished = nullptr;
    //}
    //if (RenderFinishedAfterShadow)
    //{
    //    g_rhi->GetSemaphoreManager()->ReturnSemaphore(RenderFinishedAfterShadow);
    //    RenderFinishedAfterShadow = nullptr;
    //}
    //if (RenderFinishedAfterBasePass)
    //{
    //    g_rhi->GetSemaphoreManager()->ReturnSemaphore(RenderFinishedAfterBasePass);
    //    RenderFinishedAfterBasePass = nullptr;
    //}
}

//////////////////////////////////////////////////////////////////////////
// jSwapchain_DX12
//////////////////////////////////////////////////////////////////////////
bool jSwapchain_DX12::Create()
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.BufferCount = jRHI_DX12::MaxFrameCount;
    swapChainDesc.Width = SCR_WIDTH;
    swapChainDesc.Height = SCR_HEIGHT;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        //| DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    check(g_rhi_dx12);
    check(!!g_rhi_dx12->m_hWnd);
    check(g_rhi_dx12->Factory);
    check(g_rhi_dx12->CommandBufferManager);
    
    ComPtr<IDXGISwapChain1> swapChainTemp;
    if (JFAIL(g_rhi_dx12->Factory->CreateSwapChainForHwnd(g_rhi_dx12->CommandBufferManager->GetCommandQueue().Get(), g_rhi_dx12->m_hWnd
        , &swapChainDesc, nullptr, nullptr, &swapChainTemp)))
    {
        return false;
    }

    if (JFAIL(swapChainTemp->QueryInterface(IID_PPV_ARGS(&SwapChain))))
        return false;

    Extent = Vector2i(SCR_WIDTH, SCR_HEIGHT);

    Images.resize(jRHI_DX12::MaxFrameCount);
    for (int32 i = 0; i < Images.size(); ++i)
    {
        jSwapchainImage_DX12* SwapchainImage = new jSwapchainImage_DX12();

        ComPtr<ID3D12Resource> renderTarget;
        if (JFAIL(SwapChain->GetBuffer(i, IID_PPV_ARGS(&renderTarget))))
            return false;

        Images[i] = SwapchainImage;        

        auto TextureDX12Ptr = std::make_shared<jTexture_DX12>(
            ETextureType::TEXTURE_2D, Format, Extent.x, Extent.y, 1, EMSAASamples::COUNT_1, 1, false, renderTarget);
        SwapchainImage->TexturePtr = TextureDX12Ptr;

        jBufferUtil_DX12::CreateRenderTargetView((jTexture_DX12*)SwapchainImage->TexturePtr.get());
        TextureDX12Ptr->Layout = EImageLayout::PRESENT_SRC;

        if (ensure(g_rhi_dx12->GetFenceManager()))
            SwapchainImage->CommandBufferFence = (jFence_DX12*)g_rhi_dx12->GetFenceManager()->GetOrCreateFence();
    }

    if (ensure(g_rhi_dx12->GetFenceManager()))
        Fence = (jFence_DX12*)g_rhi_dx12->GetFenceManager()->GetOrCreateFence();

    return true;
}

void jSwapchain_DX12::Release()
{
    ReleaseInternal();
}

void jSwapchain_DX12::ReleaseInternal()
{
    if (Fence)
    {
        Fence->WaitForFence();
        if (ensure(g_rhi_dx12->GetFenceManager()))
            g_rhi_dx12->GetFenceManager()->ReturnFence(Fence);

        Fence = nullptr;
    }

    for (auto& iter : Images)
    {
        delete iter;
    }
    Images.clear();

    if (SwapChain)
    {
        //SwapChain->Release();
        SwapChain = nullptr;
    }
}
