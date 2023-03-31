#include "pch.h"
#include "jBufferUtil_DX12.h"
#include "jRHI_DX12.h"
#include "jBuffer_DX12.h"
#include "jTexture_DX12.h"

namespace jBufferUtil_DX12
{

ComPtr<ID3D12Resource> CreateBufferInternal(uint64 InSize, uint16 InAlignment, bool InIsCPUAccess, bool InAllowUAV
    , D3D12_RESOURCE_STATES InInitialState, const wchar_t* InResourceName)
{
    InSize = (InAlignment > 0) ? Align(InSize, InAlignment) : InSize;

    D3D12_RESOURCE_DESC resourceDesc = { };
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = InSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.Flags = InAllowUAV ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Alignment = 0;

    D3D12_RESOURCE_STATES resourceState = InInitialState;
    if (InIsCPUAccess)
    {
        resourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
    }

    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    const D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(InIsCPUAccess ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);

    ComPtr<ID3D12Resource> Buffer;
    if (JFAIL(g_rhi_dx12->Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, resourceState, nullptr, IID_PPV_ARGS(&Buffer))))
    {
        return nullptr;
    }

    if (InResourceName)
        Buffer->SetName(InResourceName);

    return Buffer;
}

jBuffer_DX12* CreateBuffer(uint64 InSize, uint16 InAlignment, bool InIsCPUAccess, bool InAllowUAV
    , D3D12_RESOURCE_STATES InInitialState, const void* InData, uint64 InDataSize, const wchar_t* InResourceName)
{
    ComPtr<ID3D12Resource> BufferInternal = CreateBufferInternal(InSize, InAlignment, InIsCPUAccess, InAllowUAV, InInitialState, InResourceName);
    if (!BufferInternal)
        return nullptr;

    auto Buffer = new jBuffer_DX12(BufferInternal, InSize, InAlignment, InIsCPUAccess, InAllowUAV);

    const bool HasInitialData = InData && (InDataSize > 0);
    if (HasInitialData)
    {
        if (InIsCPUAccess)
        {
            void* CPUAddress = Buffer->Map();
            check(CPUAddress);
            memcpy(CPUAddress, InData, InDataSize);
        }
        else
        {
            ComPtr<ID3D12Resource> StagingBuffer = CreateBufferInternal(InSize, InAlignment, true, InAllowUAV, InInitialState);
            check(StagingBuffer);

            ComPtr<ID3D12GraphicsCommandList4> commandBuffer = g_rhi_dx12->BeginSingleTimeCopyCommands();
            check(commandBuffer);

            commandBuffer->CopyBufferRegion((ID3D12Resource*)Buffer->GetHandle(), 0, StagingBuffer.Get(), 0, InSize);
            g_rhi_dx12->EndSingleTimeCopyCommands(commandBuffer);
        }
    }

    return Buffer;
}

ComPtr<ID3D12Resource> CreateImageInternal(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , D3D12_RESOURCE_DIMENSION InType, DXGI_FORMAT InFormat, bool InIsRTV, bool InIsUAV, D3D12_RESOURCE_STATES InResourceState, const wchar_t* InResourceName)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    const D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC TexDesc = { };
    TexDesc.MipLevels = InMipLevels;
    TexDesc.Format = InFormat;
    TexDesc.Width = InWidth;
    TexDesc.Height = InHeight;
    TexDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (InIsRTV)
        TexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (InIsUAV)
        TexDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    TexDesc.DepthOrArraySize = InArrayLayers;

    const uint32 StandardMSAAPattern = 0xFFFFFFFF;
    TexDesc.SampleDesc.Count = InNumOfSample;
    TexDesc.SampleDesc.Quality = InNumOfSample > 1 ? StandardMSAAPattern : 0;

    TexDesc.Dimension = InType;
    TexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    TexDesc.Alignment = 0;

    const D3D12_RESOURCE_ALLOCATION_INFO info = g_rhi_dx12->Device->GetResourceAllocationInfo(0, 1, &TexDesc);

    // ensure(TexDesc.Width == info.SizeInBytes);

    ComPtr<ID3D12Resource> Image;
    D3D12_CLEAR_VALUE clearValue = { };
    if (JFAIL(g_rhi_dx12->Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE,
        &TexDesc, InResourceState, InIsRTV ? &clearValue : nullptr, IID_PPV_ARGS(&Image))))
    {
        return nullptr;
    }

    if (InResourceName)
        Image->SetName(InResourceName);

    return Image;
}

jTexture_DX12* CreateImage(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , ETextureType InType, ETextureFormat InFormat, bool InIsRTV, bool InIsUAV, D3D12_RESOURCE_STATES InResourceState, const wchar_t* InResourceName)
{
    ComPtr<ID3D12Resource> TextureInternal = CreateImageInternal(InWidth, InHeight, InArrayLayers, InMipLevels, InNumOfSample
        , GetDX12TextureDemension(InType), GetDX12TextureFormat(InFormat), InIsRTV, InIsUAV, InResourceState, InResourceName);
    if (!ensure(TextureInternal))
        return nullptr;

    jTexture_DX12* Texture = new jTexture_DX12(InType, InFormat, InWidth, InHeight, InArrayLayers
        , EMSAASamples::COUNT_1, InMipLevels, false, TextureInternal);
    check(Texture);

    return Texture;
}

void CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, uint64 InBufferOffset, ID3D12Resource* InImage, uint32 InWidth, uint32 InHeight)
{

}

void CopyBuffer(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InSrcBuffer, ID3D12Resource* InDstBuffer, uint64 InSize, uint64 InSrcOffset, uint64 InDstOffset)
{
    check(InCommandBuffer);
    check(InSrcBuffer);
    check(InDstBuffer);

    InCommandBuffer->CopyBufferRegion(InDstBuffer, InDstOffset, InSrcBuffer, InSrcOffset, InSize);
}

void CopyBuffer(ID3D12Resource* InSrcBuffer, ID3D12Resource* InDstBuffer, uint64 InSize, uint64 InSrcOffset, uint64 InDstOffset)
{
    ComPtr<ID3D12GraphicsCommandList4> commandBuffer = g_rhi_dx12->BeginSingleTimeCopyCommands();
    CopyBuffer(commandBuffer.Get(), InSrcBuffer, InDstBuffer, InSize, InSrcOffset, InDstOffset);
    g_rhi_dx12->EndSingleTimeCopyCommands(commandBuffer);
}

const uint64 ConstantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

void CreateConstantBufferView(jBuffer_DX12* InBuffer)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->CBV.IsValid());
    InBuffer->CBV = g_rhi_dx12->SRVDescriptorHeap.Alloc();

    D3D12_CONSTANT_BUFFER_VIEW_DESC Desc;
    Desc.BufferLocation = InBuffer->GetGPUAddress();
    Desc.SizeInBytes = (uint32)((ConstantBufferAlignment > 0) ? Align(InBuffer->Size, ConstantBufferAlignment) : InBuffer->Size);

    g_rhi_dx12->Device->CreateConstantBufferView(&Desc, InBuffer->CBV.CPUHandle);
}

void CreateShaderResourceView(jBuffer_DX12* InBuffer)
{
    CreateShaderResourceView(InBuffer, (uint32)InBuffer->Size, 1);
}

void CreateShaderResourceView(jBuffer_DX12* InBuffer, uint32 InStride, uint32 InCount
    , ETextureFormat InFormat)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->SRV.IsValid());
    InBuffer->SRV = g_rhi_dx12->SRVDescriptorHeap.Alloc();

    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Format = (InFormat == ETextureFormat::MAX) ? DXGI_FORMAT_UNKNOWN : GetDX12TextureFormat(InFormat);
    Desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    Desc.Buffer.NumElements = InCount;
    Desc.Buffer.StructureByteStride = InStride;

    g_rhi_dx12->Device->CreateShaderResourceView(InBuffer->Buffer.Get()
        , &Desc, InBuffer->SRV.CPUHandle);
}

void CreateUnorderedAccessView(jBuffer_DX12* InBuffer)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->UAV.IsValid());
    InBuffer->UAV = g_rhi_dx12->SRVDescriptorHeap.Alloc();

    D3D12_UNORDERED_ACCESS_VIEW_DESC Desc = { };
    Desc.Format = DXGI_FORMAT_UNKNOWN;
    Desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    Desc.Buffer.NumElements = 1;
    Desc.Buffer.StructureByteStride = (uint32)InBuffer->Size;

    g_rhi_dx12->Device->CreateUnorderedAccessView(InBuffer->Buffer.Get(), nullptr
        , &Desc, InBuffer->UAV.CPUHandle);
}

void CreateShaderResourceView(jTexture_DX12* InTexture)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InTexture))
        return;

    check(!InTexture->SRV.IsValid());
    InTexture->SRV = g_rhi_dx12->SRVDescriptorHeap.Alloc();

    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Format = GetDX12TextureFormat(InTexture->Format);

    switch(InTexture->Type)
    {
    case ETextureType::TEXTURE_2D:
        Desc.ViewDimension = ((int32)InTexture->SampleCount > 1) 
            ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipLevels = InTexture->MipLevel;
        Desc.Texture2D.MostDetailedMip = 0;
        Desc.Texture2D.PlaneSlice = 0;
        Desc.Texture2D.ResourceMinLODClamp = 0.0f;
        break;
    case ETextureType::TEXTURE_2D_ARRAY:
        Desc.ViewDimension = ((int32)InTexture->SampleCount > 1) 
            ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        Desc.Texture2DArray.MipLevels = InTexture->MipLevel;
        Desc.Texture2DArray.MostDetailedMip = 0;
        Desc.Texture2DArray.PlaneSlice = 0;
        Desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
        Desc.Texture2DArray.ArraySize = InTexture->LayerCount;
        Desc.Texture2DArray.FirstArraySlice = 0;
        break;
    case ETextureType::TEXTURE_CUBE:
        Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        Desc.TextureCube.MipLevels = InTexture->MipLevel;
        Desc.TextureCube.MostDetailedMip = 0;
        Desc.TextureCube.ResourceMinLODClamp = 0.0f;
        break;
    default:
        check(0);
        break;
    }

    g_rhi_dx12->Device->CreateShaderResourceView(InTexture->Image.Get()
        , &Desc, InTexture->SRV.CPUHandle);
}

void CreateUnorderedAccessView(jTexture_DX12* InTexture)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InTexture))
        return;

    check(!InTexture->UAV.IsValid());
    //InTexture->UAV = g_rhi_dx12->UAVDescriptorHeap.Alloc();
    InTexture->UAV = g_rhi_dx12->SRVDescriptorHeap.Alloc();

    D3D12_UNORDERED_ACCESS_VIEW_DESC Desc = { };
    Desc.Format = GetDX12TextureFormat(InTexture->Format);
    switch (InTexture->Type)
    {
    case ETextureType::TEXTURE_2D:
        Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipSlice = 0;
        Desc.Texture2D.PlaneSlice = 0;
        break;
    case ETextureType::TEXTURE_2D_ARRAY:
        Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        Desc.Texture2DArray.ArraySize = InTexture->LayerCount;
        Desc.Texture2DArray.FirstArraySlice = 0;
        Desc.Texture2DArray.MipSlice = 0;
        Desc.Texture2DArray.PlaneSlice = 0;
        break;
    default:
        check(0);
        break;
    }

    g_rhi_dx12->Device->CreateUnorderedAccessView(InTexture->Image.Get()
        , nullptr, &Desc, InTexture->UAV.CPUHandle);
}

void CreateRenderTargetView(jTexture_DX12* InTexture)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InTexture))
        return;

    check(!InTexture->RTV.IsValid());
    InTexture->RTV = g_rhi_dx12->RTVDescriptorHeap.Alloc();

    D3D12_RENDER_TARGET_VIEW_DESC Desc = { };
    Desc.Format = GetDX12TextureFormat(InTexture->Format);
    switch (InTexture->Type)
    {
    case ETextureType::TEXTURE_2D:
        Desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipSlice = 0;
        Desc.Texture2D.PlaneSlice = 0;
        break;
    case ETextureType::TEXTURE_2D_ARRAY:
        Desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        Desc.Texture2DArray.ArraySize = InTexture->LayerCount;
        Desc.Texture2DArray.FirstArraySlice = 0;
        Desc.Texture2DArray.MipSlice = 0;
        Desc.Texture2DArray.PlaneSlice = 0;
        break;
    default:
        check(0);
        break;
    }

    g_rhi_dx12->Device->CreateRenderTargetView(InTexture->Image.Get(), &Desc, InTexture->RTV.CPUHandle);
}


}
