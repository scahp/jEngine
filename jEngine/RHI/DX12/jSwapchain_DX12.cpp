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
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
        | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

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

        ComPtr<ID3D12Resource> NewResource;
        if (JFAIL(SwapChain->GetBuffer(i, IID_PPV_ARGS(&NewResource))))
            return false;

        std::shared_ptr<jCreatedResource> RenderTargetResource = jCreatedResource::CreatedFromSwapchain(NewResource);

        Images[i] = SwapchainImage;        

        auto TextureDX12Ptr = std::make_shared<jTexture_DX12>(
            ETextureType::TEXTURE_2D, Format, Extent.x, Extent.y, 1, EMSAASamples::COUNT_1, 1, false, jRTClearValue::Invalid, RenderTargetResource);
        SwapchainImage->TexturePtr = TextureDX12Ptr;

        jBufferUtil_DX12::CreateRenderTargetView((jTexture_DX12*)SwapchainImage->TexturePtr.get());
        TextureDX12Ptr->Layout = EResourceLayout::PRESENT_SRC;
    }

    return true;
}

void jSwapchain_DX12::Release()
{
    ReleaseInternal();
}

bool jSwapchain_DX12::Resize(int32 InWidth, int32 InHeight)
{
    if (ensure(SwapChain))
    {
        for (int32 i = 0; i < g_rhi_dx12->MaxFrameCount; ++i)
        {
            jSwapchainImage* SwapchainImage = Images[i];
            auto TexDX12 = (jTexture_DX12*)SwapchainImage->TexturePtr.get();
            TexDX12->Image->Resource.Reset();
        }

        SwapChain->SetFullscreenState(false, nullptr);
        HRESULT hr = SwapChain->ResizeBuffers(g_rhi_dx12->MaxFrameCount, InWidth, InHeight, DXGI_FORMAT_R8G8B8A8_UNORM
            , DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH |
            DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
#ifdef _DEBUG
            char buff[64] = {};
            sprintf_s(buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n"
                , (hr == DXGI_ERROR_DEVICE_REMOVED) ? g_rhi_dx12->Device->GetDeviceRemovedReason() : hr);
            OutputDebugStringA(buff);
#endif
            return false;
        }
        else
        {
            JOK(hr);
        }
    }

    for (int32 i = 0; i < g_rhi_dx12->MaxFrameCount; ++i)
    {
        jSwapchainImage* SwapchainImage = Images[i];

        ComPtr<ID3D12Resource> NewResource;
        if (JFAIL(SwapChain->GetBuffer(i, IID_PPV_ARGS(&NewResource))))
            return false;

        std::shared_ptr<jCreatedResource> RenderTargetResource = jCreatedResource::CreatedFromSwapchain(NewResource);

        auto TextureDX12Ptr = std::make_shared<jTexture_DX12>(
            ETextureType::TEXTURE_2D, GetDX12TextureFormat(DXGI_FORMAT_R8G8B8A8_UNORM), InWidth, InHeight, 1, EMSAASamples::COUNT_1, 1, false, jRTClearValue::Invalid, RenderTargetResource);
        SwapchainImage->TexturePtr = TextureDX12Ptr;

        jBufferUtil_DX12::CreateRenderTargetView((jTexture_DX12*)SwapchainImage->TexturePtr.get());
    }

    g_rhi_dx12->RenderPassPool.Release();
    g_rhi_dx12->PipelineStatePool.Release();

    return true;
}

void jSwapchain_DX12::ReleaseInternal()
{
    for (auto& iter : Images)
    {
        delete iter;
    }
    Images.clear();

    if (SwapChain)
    {
        SwapChain = nullptr;
    }
}
