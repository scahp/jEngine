#include "pch.h"
#include "jBufferUtil_DX12.h"
#include "jRHI_DX12.h"
#include "jBuffer_DX12.h"
#include "jTexture_DX12.h"

namespace jBufferUtil_DX12
{

std::shared_ptr<jCreatedResource> CreateBufferInternal(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
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
    resourceDesc.Flags = !!(InBufferCreateFlag & EBufferCreateFlag::UAV) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Alignment = 0;

    D3D12_RESOURCE_STATES resourceState = InInitialState;
    if (!!(InBufferCreateFlag & EBufferCreateFlag::Readback))
    {
        resourceState = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    else if (!!(InBufferCreateFlag & EBufferCreateFlag::CPUAccess))
    {
        resourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
    }

    check(g_rhi_dx12);

    std::shared_ptr<jCreatedResource> CreatedResource;
    if (!!(InBufferCreateFlag & EBufferCreateFlag::Readback))
    {
        check(EBufferCreateFlag::NONE == (InBufferCreateFlag & EBufferCreateFlag::UAV));        // Not allowed Readback with UAV

        ComPtr<ID3D12Resource> NewResource;
        const CD3DX12_HEAP_PROPERTIES& HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        JFAIL(g_rhi_dx12->Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE
            , &resourceDesc, resourceState, nullptr, IID_PPV_ARGS(&NewResource)));

        CreatedResource = jCreatedResource::CreatedFromStandalone(NewResource);
    }
    else if (!!(InBufferCreateFlag & EBufferCreateFlag::CPUAccess))
    {
        check(EBufferCreateFlag::NONE == (InBufferCreateFlag & EBufferCreateFlag::UAV));        // Not allowed Readback with UAV
        CreatedResource = g_rhi_dx12->CreateUploadResource(&resourceDesc, resourceState);
    }
    else
    {
        CreatedResource = g_rhi_dx12->CreateResource(&resourceDesc, resourceState);
    }

    ensure(CreatedResource->Resource);

    if (InResourceName && CreatedResource->Resource)
        CreatedResource->Resource->SetName(InResourceName);

    return CreatedResource;
}

std::shared_ptr<jBuffer_DX12> CreateBuffer(uint64 InSize, uint64 InAlignment, EBufferCreateFlag InBufferCreateFlag
    , D3D12_RESOURCE_STATES InInitialState /*= D3D12_RESOURCE_STATE_COMMON*/, const void* InData /*= nullptr*/, uint64 InDataSize /*= 0*/, const wchar_t* InResourceName /*= nullptr*/)
{
    std::shared_ptr<jCreatedResource> BufferInternal = CreateBufferInternal(InSize, InAlignment, InBufferCreateFlag, InInitialState, InResourceName);
    if (!BufferInternal->Resource)
        return nullptr;

    auto BufferPtr = std::make_shared<jBuffer_DX12>(BufferInternal, InSize, InAlignment, InBufferCreateFlag);
    if (InResourceName)
    {
        // https://learn.microsoft.com/ko-kr/cpp/text/how-to-convert-between-various-string-types?view=msvc-170#example-convert-from-char-
        char szResourceName[1024];
        size_t OutLength = 0;
        size_t origsize = wcslen(InResourceName) + 1;
        const size_t newsize = origsize * 2;
        wcstombs_s(&OutLength, szResourceName, newsize, InResourceName, _TRUNCATE);

        BufferPtr->ResourceName = jName(szResourceName);
    }

    const bool HasInitialData = InData && (InDataSize > 0);
    if (HasInitialData)
    {
        if (!!(InBufferCreateFlag & EBufferCreateFlag::Readback))
        {
            // nothing todo
        }
        else if (!!(InBufferCreateFlag & EBufferCreateFlag::CPUAccess))
        {
            void* CPUAddress = BufferPtr->Map();
            check(CPUAddress);
            memcpy(CPUAddress, InData, InDataSize);
        }
        else
        {
            std::shared_ptr<jCreatedResource> StagingBuffer = CreateBufferInternal(InSize, InAlignment, EBufferCreateFlag::CPUAccess, InInitialState, InResourceName);
            check(StagingBuffer->IsValid());

            void* MappedPointer = nullptr;
            D3D12_RANGE Range = {};
            if (JOK(StagingBuffer->Resource->Map(0, &Range, &MappedPointer)))
            {
                check(MappedPointer);
                memcpy(MappedPointer, InData, InDataSize);
                StagingBuffer->Resource->Unmap(0, nullptr);
            }

            jCommandBuffer_DX12* commandBuffer = g_rhi_dx12->BeginSingleTimeCopyCommands();
            check(commandBuffer->IsValid());

            commandBuffer->Get()->CopyBufferRegion((ID3D12Resource*)BufferPtr->GetHandle(), 0, StagingBuffer->Get(), 0, InSize);
            g_rhi_dx12->EndSingleTimeCopyCommands(commandBuffer);
        }
    }

    return BufferPtr;
}

std::shared_ptr<jCreatedResource> CreateImageInternal(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , D3D12_RESOURCE_DIMENSION InType, DXGI_FORMAT InFormat, ETextureCreateFlag InTextureCreateFlag, EImageLayout InImageLayout, D3D12_CLEAR_VALUE* InClearValue, const wchar_t* InResourceName)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    D3D12_RESOURCE_DESC TexDesc = { };
    TexDesc.MipLevels = InMipLevels;
    if (IsDepthFormat(GetDX12TextureFormat(InFormat)))
    {
        DXGI_FORMAT TexFormat, SrvFormat;
        GetDepthFormatForSRV(TexFormat, SrvFormat, InFormat);
        TexDesc.Format = TexFormat;
    }
    else
    {
        TexDesc.Format = InFormat;
    }
    TexDesc.Width = InWidth;
    TexDesc.Height = InHeight;
    TexDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (!!(InTextureCreateFlag & ETextureCreateFlag::RTV))
        TexDesc.Flags = IsDepthFormat(GetDX12TextureFormat(InFormat)) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (!!(InTextureCreateFlag & ETextureCreateFlag::UAV))
        TexDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    TexDesc.DepthOrArraySize = InArrayLayers;

    const uint32 StandardMSAAPattern = 0xFFFFFFFF;
    TexDesc.SampleDesc.Count = InNumOfSample;
    TexDesc.SampleDesc.Quality = InNumOfSample > 1 ? StandardMSAAPattern : 0;

    TexDesc.Dimension = InType;
    TexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    TexDesc.Alignment = 0;

    std::shared_ptr<jCreatedResource> ImageResource = g_rhi_dx12->CreateResource(&TexDesc, GetDX12ImageLayout(InImageLayout), InClearValue);
    ensure(ImageResource->Resource);

    if (InResourceName && ImageResource->Resource)
        ImageResource->Resource->SetName(InResourceName);

    return ImageResource;
}

jTexture_DX12* CreateImage(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , ETextureType InType, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag, EImageLayout InImageLayout, const jRTClearValue& InClearValue, const wchar_t* InResourceName)
{
    bool HasClearValue = false;
    D3D12_CLEAR_VALUE ClearValue{};
    if (!!(InTextureCreateFlag & ETextureCreateFlag::RTV))
    {
        if (InClearValue.GetType() == ERTClearType::Color)
        {
            ClearValue.Color[0] = InClearValue.GetCleraColor()[0];
            ClearValue.Color[1] = InClearValue.GetCleraColor()[1];
            ClearValue.Color[2] = InClearValue.GetCleraColor()[2];
            ClearValue.Color[3] = InClearValue.GetCleraColor()[3];
            ClearValue.Format = GetDX12TextureFormat(InFormat);
        }
        else if (InClearValue.GetType() == ERTClearType::DepthStencil)
        {
            ClearValue.DepthStencil.Depth = InClearValue.GetCleraDepth();
            ClearValue.DepthStencil.Stencil = InClearValue.GetCleraStencil();
            ClearValue.Format = GetDX12TextureFormat(InFormat);
        }
        HasClearValue = InClearValue.GetType() != ERTClearType::None;
    }

    std::shared_ptr<jCreatedResource> TextureInternal = CreateImageInternal(InWidth, InHeight, InArrayLayers, InMipLevels, InNumOfSample
        , GetDX12TextureDemension(InType), GetDX12TextureFormat(InFormat), InTextureCreateFlag, InImageLayout, (HasClearValue ? &ClearValue : nullptr), InResourceName);
    ensure(TextureInternal->IsValid());

    jTexture_DX12* Texture = new jTexture_DX12(InType, InFormat, InWidth, InHeight, InArrayLayers
        , EMSAASamples::COUNT_1, InMipLevels, false, InClearValue, TextureInternal);
    check(Texture);
    Texture->Layout = InImageLayout;

    if (InResourceName)
    {
        // https://learn.microsoft.com/ko-kr/cpp/text/how-to-convert-between-various-string-types?view=msvc-170#example-convert-from-char-
        char szResourceName[1024];
        size_t OutLength = 0;
        size_t origsize = wcslen(InResourceName) + 1;
        const size_t newsize = origsize * 2;
        wcstombs_s(&OutLength, szResourceName, newsize, InResourceName, _TRUNCATE);

        Texture->ResourceName = jName(szResourceName);
    }

    if (IsDepthFormat(InFormat))
    {
        CreateShaderResourceView(Texture);
        CreateDepthStencilView(Texture);
    }
    else
    {
        CreateShaderResourceView(Texture);
        if (!!(InTextureCreateFlag & ETextureCreateFlag::RTV))
            CreateRenderTargetView(Texture);
        if (!!(InTextureCreateFlag & ETextureCreateFlag::UAV))
            CreateUnorderedAccessView(Texture);
    }

    return Texture;
}

jTexture_DX12* CreateImage(const std::shared_ptr<jCreatedResource>& InTexture, ETextureCreateFlag InTextureCreateFlag, EImageLayout InImageLayout, const jRTClearValue& InClearValue, const wchar_t* InResourceName)
{
    const auto desc = InTexture->Resource->GetDesc();
    jTexture_DX12* Texture = new jTexture_DX12(GetDX12TextureDemension(desc.Dimension, desc.DepthOrArraySize > 1), GetDX12TextureFormat(desc.Format), (int32)desc.Width, (int32)desc.Height, (int32)desc.DepthOrArraySize
        , EMSAASamples::COUNT_1, (int32)desc.MipLevels, false, InClearValue, InTexture);

    check(Texture);
    Texture->Layout = InImageLayout;

    if (InResourceName)
    {
        // https://learn.microsoft.com/ko-kr/cpp/text/how-to-convert-between-various-string-types?view=msvc-170#example-convert-from-char-
        char szResourceName[1024];
        size_t OutLength = 0;
        size_t origsize = wcslen(InResourceName) + 1;
        const size_t newsize = origsize * 2;
        wcstombs_s(&OutLength, szResourceName, newsize, InResourceName, _TRUNCATE);

        Texture->ResourceName = jName(szResourceName);
    }

    if (IsDepthFormat(GetDX12TextureFormat(desc.Format)))
    {
        CreateShaderResourceView(Texture);
        CreateDepthStencilView(Texture);
    }
    else
    {
        CreateShaderResourceView(Texture);
        if (!!(InTextureCreateFlag & ETextureCreateFlag::RTV))
            CreateRenderTargetView(Texture);
        if (!!(InTextureCreateFlag & ETextureCreateFlag::UAV))
            CreateUnorderedAccessView(Texture);
    }

    return Texture;
}

uint64 CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, uint64 InBufferOffset, ID3D12Resource* InImage, int32 InImageSubresourceIndex)
{
    check(InCommandBuffer);

    const auto imageDesc = InImage->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
    uint32 numRow = 0;
    uint64 rowSize = 0;
    uint64 textureMemorySize = 0;
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);
    g_rhi_dx12->Device->GetCopyableFootprints(&imageDesc, 0, 1, 0, &layout, &numRow, &rowSize, &textureMemorySize);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = InImage;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = InImageSubresourceIndex;
    
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = InBuffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layout;
    src.PlacedFootprint.Offset = InBufferOffset;
    InCommandBuffer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    return textureMemorySize;
}

uint64 CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, uint64 InBufferOffset, ID3D12Resource* InImage
    , int32 InNumOfImageSubresource, int32 InStartImageSubresource)
{
    for (int32 i = 0; i < InNumOfImageSubresource; ++i)
    {
        InBufferOffset += CopyBufferToImage(InCommandBuffer, InBuffer, InBufferOffset, InImage, i);
    }
    return InBufferOffset;      // total size of copy data
}

void CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, ID3D12Resource* InImage, const std::vector<jImageSubResourceData>& InSubresourceData)
{
    for (uint64 i = 0; i < InSubresourceData.size(); ++i)
    {
        D3D12_TEXTURE_COPY_LOCATION dst = { };
        dst.pResource = InImage;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = uint32(i);
        D3D12_TEXTURE_COPY_LOCATION src = { };
        src.pResource = InBuffer;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = (DXGI_FORMAT)InSubresourceData[i].Format;
        src.PlacedFootprint.Footprint.Width = InSubresourceData[i].Width;
        src.PlacedFootprint.Footprint.Height = InSubresourceData[i].Height;
        src.PlacedFootprint.Footprint.Depth = InSubresourceData[i].Depth;
        src.PlacedFootprint.Footprint.RowPitch = InSubresourceData[i].RowPitch;
        src.PlacedFootprint.Offset = InSubresourceData[i].Offset;
        InCommandBuffer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
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
    jCommandBuffer_DX12* commandBuffer = g_rhi_dx12->BeginSingleTimeCopyCommands();
    CopyBuffer(commandBuffer->Get(), InSrcBuffer, InDstBuffer, InSize, InSrcOffset, InDstOffset);
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
    InBuffer->CBV = g_rhi_dx12->DescriptorHeaps.Alloc();

    D3D12_CONSTANT_BUFFER_VIEW_DESC Desc;
    Desc.BufferLocation = InBuffer->GetGPUAddress();
    Desc.SizeInBytes = (uint32)((ConstantBufferAlignment > 0) ? Align(InBuffer->Size, ConstantBufferAlignment) : InBuffer->Size);

    g_rhi_dx12->Device->CreateConstantBufferView(&Desc, InBuffer->CBV.CPUHandle);
}

void CreateShaderResourceView_StructuredBuffer(jBuffer_DX12* InBuffer, uint32 InStride, uint32 InCount)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->SRV.IsValid());
    InBuffer->SRV = g_rhi_dx12->DescriptorHeaps.Alloc();

    D3D12_SHADER_RESOURCE_VIEW_DESC Desc{};
    Desc.Format = DXGI_FORMAT_UNKNOWN;
    Desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    Desc.Buffer.NumElements = InCount;
    Desc.Buffer.StructureByteStride = InStride;
    g_rhi_dx12->Device->CreateShaderResourceView(InBuffer->Buffer->Get(), &Desc, InBuffer->SRV.CPUHandle);
}

void CreateShaderResourceView_Raw(jBuffer_DX12* InBuffer, uint32 InBufferSize)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->SRV.IsValid());
    InBuffer->SRV = g_rhi_dx12->DescriptorHeaps.Alloc();

    D3D12_SHADER_RESOURCE_VIEW_DESC Desc{};
    Desc.Format = DXGI_FORMAT_R32_TYPELESS;
    Desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    Desc.Buffer.NumElements = InBufferSize / 4;     // DXGI_FORMAT_R32_TYPELESS size is 4
    g_rhi_dx12->Device->CreateShaderResourceView(InBuffer->Buffer->Get(), &Desc, InBuffer->SRV.CPUHandle);
}

void CreateShaderResourceView_Formatted(jBuffer_DX12* InBuffer, ETextureFormat InFormat, uint32 InBufferSize)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->SRV.IsValid());
    InBuffer->SRV = g_rhi_dx12->DescriptorHeaps.Alloc();

    const uint32 Stride = GetDX12TextureComponentCount(InFormat) * GetDX12TexturePixelSize(InFormat);

    D3D12_SHADER_RESOURCE_VIEW_DESC Desc{};
    Desc.Format = GetDX12TextureFormat(InFormat);
    Desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    Desc.Buffer.NumElements = InBufferSize / Stride;
    g_rhi_dx12->Device->CreateShaderResourceView(InBuffer->Buffer->Get(), &Desc, InBuffer->SRV.CPUHandle);
}

void CreateUnorderedAccessView_StructuredBuffer(jBuffer_DX12* InBuffer, uint32 InStride, uint32 InCount)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->UAV.IsValid());
    InBuffer->UAV = g_rhi_dx12->DescriptorHeaps.Alloc();

    D3D12_UNORDERED_ACCESS_VIEW_DESC Desc{ };
    Desc.Format = DXGI_FORMAT_UNKNOWN;
    Desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    Desc.Buffer.NumElements = InCount;
    Desc.Buffer.StructureByteStride = InStride;
    g_rhi_dx12->Device->CreateUnorderedAccessView(InBuffer->Buffer->Get(), nullptr, &Desc, InBuffer->UAV.CPUHandle);
}

void CreateUnorderedAccessView_Raw(jBuffer_DX12* InBuffer, uint32 InBufferSize)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->UAV.IsValid());
    InBuffer->UAV = g_rhi_dx12->DescriptorHeaps.Alloc();

    D3D12_UNORDERED_ACCESS_VIEW_DESC Desc{ };
    Desc.Format = DXGI_FORMAT_R32_TYPELESS;
    Desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    Desc.Buffer.NumElements = InBufferSize / 4;     // DXGI_FORMAT_R32_TYPELESS size is 4
    g_rhi_dx12->Device->CreateUnorderedAccessView(InBuffer->Buffer->Get(), nullptr, &Desc, InBuffer->UAV.CPUHandle);
}

void CreateUnorderedAccessView_Formatted(jBuffer_DX12* InBuffer, ETextureFormat InFormat, uint32 InBufferSize)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InBuffer))
        return;

    check(!InBuffer->UAV.IsValid());
    InBuffer->UAV = g_rhi_dx12->DescriptorHeaps.Alloc();

    const uint32 Stride = GetDX12TextureComponentCount(InFormat) * GetDX12TexturePixelSize(InFormat);

    D3D12_UNORDERED_ACCESS_VIEW_DESC Desc{ };
    Desc.Format = GetDX12TextureFormat(InFormat);
    Desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    Desc.Buffer.FirstElement = 0;
    Desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    Desc.Buffer.NumElements = InBufferSize / Stride;
    g_rhi_dx12->Device->CreateUnorderedAccessView(InBuffer->Buffer->Get(), nullptr, &Desc, InBuffer->UAV.CPUHandle);
}

void CreateShaderResourceView(jTexture_DX12* InTexture)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InTexture))
        return;

    check(!InTexture->SRV.IsValid());
    InTexture->SRV = g_rhi_dx12->DescriptorHeaps.Alloc();

    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (IsDepthFormat(InTexture->Format))
    {
        DXGI_FORMAT TexFormat, SrvFormat;
        GetDepthFormatForSRV(TexFormat, SrvFormat, GetDX12TextureFormat(InTexture->Format));
        Desc.Format = SrvFormat;
    }
    else
    {
        Desc.Format = GetDX12TextureFormat(InTexture->Format);
    }

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

    g_rhi_dx12->Device->CreateShaderResourceView(InTexture->Image->Get()
        , &Desc, InTexture->SRV.CPUHandle);
}

void CreateDepthStencilView(jTexture_DX12* InTexture)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InTexture))
        return;

    check(!InTexture->DSV.IsValid());
    InTexture->DSV = g_rhi_dx12->DSVDescriptorHeaps.Alloc();
    D3D12_DEPTH_STENCIL_VIEW_DESC Desc = {};

    Desc.Format = GetDX12TextureFormat(InTexture->Format);
    const bool IsMultisampled = ((int32)InTexture->SampleCount > 1);
    switch (InTexture->Type)
    {
    case ETextureType::TEXTURE_2D:
        Desc.ViewDimension = IsMultisampled
            ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
        Desc.Texture2D.MipSlice = 0;
        break;
    case ETextureType::TEXTURE_2D_ARRAY:
    case ETextureType::TEXTURE_CUBE:
        if (IsMultisampled)
        {
            Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
            Desc.Texture2DMSArray.FirstArraySlice = 0;
            Desc.Texture2DMSArray.ArraySize = InTexture->LayerCount;
        }
        else
        {
            Desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            Desc.Texture2DArray.FirstArraySlice = 0;
            Desc.Texture2DArray.ArraySize = InTexture->LayerCount;
            Desc.Texture2DArray.MipSlice = 0;
        }
        break;
    default:
        check(0);
        break;
    }

    //const bool HasStencil = !IsDepthOnlyFormat(InTexture->Format);
    //Desc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    //if (HasStencil)
    //    Desc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    Desc.Flags = D3D12_DSV_FLAG_NONE;

    g_rhi_dx12->Device->CreateDepthStencilView(InTexture->Image->Get()
        , &Desc, InTexture->DSV.CPUHandle);
}

void CreateUnorderedAccessView(jTexture_DX12* InTexture)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InTexture))
        return;

    check(!InTexture->UAV.IsValid());
    check(InTexture->MipLevel > 0);
    for (int32 i = 0; i < InTexture->MipLevel; ++i)
    {
        jDescriptor_DX12 UAV = g_rhi_dx12->DescriptorHeaps.Alloc();

        D3D12_UNORDERED_ACCESS_VIEW_DESC Desc = { };
        Desc.Format = GetDX12TextureFormat(InTexture->Format);
        switch (InTexture->Type)
        {
        case ETextureType::TEXTURE_2D:
            Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            Desc.Texture2D.MipSlice = i;
            Desc.Texture2D.PlaneSlice = 0;
            break;
        case ETextureType::TEXTURE_2D_ARRAY:
            Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            Desc.Texture2DArray.ArraySize = InTexture->LayerCount;
            Desc.Texture2DArray.FirstArraySlice = 0;
            Desc.Texture2DArray.MipSlice = i;
            Desc.Texture2DArray.PlaneSlice = 0;
            break;
        case ETextureType::TEXTURE_CUBE:
            Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            Desc.Texture2DArray.ArraySize = InTexture->LayerCount;
            Desc.Texture2DArray.FirstArraySlice = 0;
            Desc.Texture2DArray.MipSlice = i;
            Desc.Texture2DArray.PlaneSlice = 0;
            break;
        default:
            check(0);
            break;
        }

        g_rhi_dx12->Device->CreateUnorderedAccessView(InTexture->Image->Get()
            , nullptr, &Desc, UAV.CPUHandle);
     
        if (i == 0)
        {
            InTexture->UAV = UAV;
        }

        InTexture->UAVMipMap[i] = UAV;
    }
}

void CreateRenderTargetView(jTexture_DX12* InTexture)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    if (!ensure(InTexture))
        return;

    check(!InTexture->RTV.IsValid());
    InTexture->RTV = g_rhi_dx12->RTVDescriptorHeaps.Alloc();

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
    case ETextureType::TEXTURE_CUBE:
        check(InTexture->LayerCount == 6);
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

    g_rhi_dx12->Device->CreateRenderTargetView(InTexture->Image->Get(), &Desc, InTexture->RTV.CPUHandle);
}


}
