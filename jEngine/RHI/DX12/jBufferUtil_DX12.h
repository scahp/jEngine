#pragma once
#include "jRHIType_DX12.h"
#include "jDescriptorHeap_DX12.h"

struct jBuffer_DX12;
struct jTexture_DX12;

namespace
{
size_t BitsPerPixel_(DXGI_FORMAT fmt)
{
    switch (static_cast<int>(fmt))
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 128;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 96;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        return 64;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_YUY2:
    //case XBOX_DXGI_FORMAT_R10G10B10_7E3_A2_FLOAT:
    //case XBOX_DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT:
    //case XBOX_DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM:
        return 32;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
    //case XBOX_DXGI_FORMAT_D16_UNORM_S8_UINT:
    //case XBOX_DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
    //case XBOX_DXGI_FORMAT_X16_TYPELESS_G8_UINT:
    //case WIN10_DXGI_FORMAT_V408:
        return 24;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
    //case WIN10_DXGI_FORMAT_P208:
    //case WIN10_DXGI_FORMAT_V208:
        return 16;

    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_NV11:
        return 12;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
    //case XBOX_DXGI_FORMAT_R4G4_UNORM:
        return 8;

    case DXGI_FORMAT_R1_UNORM:
        return 1;

    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 4;

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 8;

    default:
        return 0;
    }
}

size_t BytesPerPixel_(DXGI_FORMAT fmt)
{
    return BitsPerPixel_(fmt) / 8;
}

const D3D12_HEAP_PROPERTIES& GetUploadHeap()
{
    static D3D12_HEAP_PROPERTIES HeapProp{ .Type = D3D12_HEAP_TYPE_UPLOAD, .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN
        , .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN, .CreationNodeMask = 0, .VisibleNodeMask = 0 };
    return HeapProp;
}

const D3D12_HEAP_PROPERTIES& GetDefaultHeap()
{
    static D3D12_HEAP_PROPERTIES HeapProp{ .Type = D3D12_HEAP_TYPE_DEFAULT, .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN
        , .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN, .CreationNodeMask = 0, .VisibleNodeMask = 0 };
    return HeapProp;
}

const D3D12_RESOURCE_DESC& GetUploadResourceDesc(uint64 InSize)
{
    static D3D12_RESOURCE_DESC Desc{
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = InSize,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };

    return Desc;
}

ComPtr<ID3D12Resource> CreateStagingBuffer(const void* InInitData, int64 InSize, uint64 InAlignment = 1)
{
    const uint64 AlignedSize = Align(InSize, InAlignment);

    ComPtr<ID3D12Resource> UploadResourceRHI;
    g_rhi_dx12->Device->CreateCommittedResource(&GetUploadHeap(), D3D12_HEAP_FLAG_NONE, &GetUploadResourceDesc(AlignedSize)
        , D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&UploadResourceRHI));

    void* MappedPointer = nullptr;
    D3D12_RANGE Range = {};
    UploadResourceRHI->Map(0, &Range, reinterpret_cast<void**>(&MappedPointer));
    memcpy(MappedPointer, InInitData, InSize);
    UploadResourceRHI->Unmap(0, &Range);
    return UploadResourceRHI;
}

void UploadByUsingStagingBuffer(ComPtr<ID3D12Resource>& DestBuffer, const void* InInitData, uint64 InSize, uint64 InAlignment = 1)
{
    check(DestBuffer);

    const uint64 AlignedSize = Align(InSize, InAlignment);
    check(DestBuffer->GetDesc().Width >= AlignedSize);

    ComPtr<ID3D12Resource> StagingBuffer = CreateStagingBuffer(InInitData, InSize, InAlignment);

    jCommandBuffer_DX12* commandBuffer = g_rhi_dx12->BeginSingleTimeCopyCommands();
    check(commandBuffer->IsValid());
    commandBuffer->Get()->CopyBufferRegion(DestBuffer.Get(), 0, StagingBuffer.Get(), 0, AlignedSize);
    g_rhi_dx12->EndSingleTimeCopyCommands(commandBuffer);
}

D3D12_RESOURCE_DESC GetDefaultResourceDesc(uint64 InAlignedSize, bool InIsAllowUAV)
{
    static D3D12_RESOURCE_DESC Desc{
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = InAlignedSize,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = InIsAllowUAV ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE,
    };

    return Desc;
}

ComPtr<ID3D12Resource> CreateDefaultResource(uint64 InAlignedSize, D3D12_RESOURCE_STATES InInitialState
    , bool InIsAllowUAV, bool InIsCPUAccessible, const wchar_t* InName = nullptr)
{
    const D3D12_RESOURCE_DESC Desc = GetDefaultResourceDesc(InAlignedSize, false);
    const D3D12_HEAP_PROPERTIES HeapProperties = InIsCPUAccessible ? GetUploadHeap() : GetDefaultHeap();

    D3D12_RESOURCE_STATES ResourceState = InInitialState;
    if (InIsCPUAccessible)
    {
        ResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
    }

    ComPtr<ID3D12Resource> NewResourceRHI;
    if (SUCCEEDED(g_rhi_dx12->Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE
        , &Desc, ResourceState, nullptr, IID_PPV_ARGS(&NewResourceRHI))))
    {
        if (InName)
            NewResourceRHI->SetName(InName);
    }

    return NewResourceRHI;
}

void* CopyInitialData(ComPtr<ID3D12Resource>& InDest, const void* InInitData, uint64 InSize, uint64 InAlignment, bool InIsCPUAccessible)
{
    if (InDest)
    {
        if (InIsCPUAccessible)
        {
            void* MappedPointer = nullptr;
            D3D12_RANGE Range = {};
            InDest->Map(0, &Range, reinterpret_cast<void**>(&MappedPointer));
            check(MappedPointer);

            if (InInitData && MappedPointer)
            {
                memcpy(MappedPointer, InInitData, InSize);
            }
            return MappedPointer;
        }
        else
        {
            if (InInitData)
            {
                UploadByUsingStagingBuffer(InDest, InInitData, InSize, InAlignment);
            }
        }
    }
    return nullptr;
}
}

struct jConstantBuffer
{
    uint64 Size = 0;
    bool IsCPUAccessible = true;
    const void* InitData = nullptr;
    D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    const wchar_t* Name = nullptr;

    ComPtr<ID3D12Resource> ResourceRHI;
    uint8* MappedPointer = 0;

    void Initialize()
    {
        const int32 ConstantBufferAlignment = 256;

        const D3D12_RESOURCE_STATES InitialResourceState = InitData ? D3D12_RESOURCE_STATE_COMMON : InitialState;
        const uint64 AlignedSize = Align(Size, ConstantBufferAlignment);
        ResourceRHI = CreateDefaultResource(AlignedSize, InitialResourceState, false, IsCPUAccessible, Name);
        CopyInitialData(ResourceRHI, InitData, Size, ConstantBufferAlignment, IsCPUAccessible);
    }

    void Release()
    {
        ResourceRHI.Reset();
    }

    void* Map()
    {
        return MappedPointer;
    }

    void BindGraphics(ID3D12GraphicsCommandList4* InCmdList, uint32 InRootParameter) const
    {
        InCmdList->SetGraphicsRootConstantBufferView(InRootParameter, ResourceRHI->GetGPUVirtualAddress());
    }

    void BindCompute(ID3D12GraphicsCommandList4* InCmdList, uint32 InRootParameter) const
    {
        InCmdList->SetComputeRootConstantBufferView(InRootParameter, ResourceRHI->GetGPUVirtualAddress());
    }
};

struct jStructuredBuffer
{
    uint64 Stride = 0;
    uint64 NumElements = 0;
    bool IsAllowUAV = false;
    jDescriptor_DX12 SRV;
    jDescriptor_DX12 UAV;
    bool IsCPUAccessible = false;
    const void* InitData = nullptr;
    D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    const wchar_t* Name = nullptr;
    ComPtr<ID3D12Resource> ResourceRHI;

    void Initialize()
    {
        const D3D12_RESOURCE_STATES InitialResourceState = InitData ? D3D12_RESOURCE_STATE_COMMON : InitialState;
        const uint64 AlignedSize = Align(Stride * NumElements, Stride);
        ResourceRHI = CreateDefaultResource(AlignedSize, InitialResourceState, false, IsCPUAccessible, Name);
        CopyInitialData(ResourceRHI, InitData, Stride * NumElements, Stride, IsCPUAccessible);

        // SRV 생성
        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc;
        SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Buffer.FirstElement = 0;
        SrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        SrvDesc.Buffer.NumElements = uint32(NumElements);
        SrvDesc.Buffer.StructureByteStride = uint32(Stride);

        SRV = g_rhi_dx12->DescriptorHeaps.Alloc();
        g_rhi_dx12->Device->CreateShaderResourceView(ResourceRHI.Get(), &SrvDesc, SRV.CPUHandle);

        // UAV 생성
        if (IsAllowUAV)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc = { };
            UavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            UavDesc.Format = DXGI_FORMAT_UNKNOWN;
            UavDesc.Buffer.CounterOffsetInBytes = 0;
            UavDesc.Buffer.FirstElement = 0;
            UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            UavDesc.Buffer.NumElements = uint32(NumElements);
            UavDesc.Buffer.StructureByteStride = uint32(Stride);

            UAV = g_rhi_dx12->DescriptorHeaps.Alloc();
            g_rhi_dx12->Device->CreateUnorderedAccessView(ResourceRHI.Get(), nullptr, &UavDesc, UAV.CPUHandle);
        }
    }

    void Release()
    {
        SRV.Free();
        UAV.Free();

        ResourceRHI.Reset();
    }
};

struct jFormattedBuffer
{
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    uint64 NumElements = 0;
    bool IsAllowUAV = false;
    bool IsCPUAccessible = false;
    const void* InitData = nullptr;
    D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    const wchar_t* Name = nullptr;
    ComPtr<ID3D12Resource> ResourceRHI;
    jDescriptor_DX12 SRV;
    jDescriptor_DX12 UAV;

    void Initialize()
    {
        const uint64 Stride = BitsPerPixel_(Format) / 8;

        const D3D12_RESOURCE_STATES InitialResourceState = InitData ? D3D12_RESOURCE_STATE_COMMON : InitialState;
        const uint64 AlignedSize = Align(Stride * NumElements, Stride);
        ResourceRHI = CreateDefaultResource(AlignedSize, InitialResourceState, false, IsCPUAccessible, Name);
        CopyInitialData(ResourceRHI, InitData, Stride * NumElements, Stride, IsCPUAccessible);

        // SRV 생성
        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc;
        SrvDesc.Format = Format;
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Buffer.FirstElement = 0;
        SrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        SrvDesc.Buffer.NumElements = uint32(NumElements);

        SRV = g_rhi_dx12->DescriptorHeaps.Alloc();
        g_rhi_dx12->Device->CreateShaderResourceView(ResourceRHI.Get(), &SrvDesc, SRV.CPUHandle);

        // UAV 생성
        if (IsAllowUAV)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc = { };
            UavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            UavDesc.Format = Format;
            UavDesc.Buffer.CounterOffsetInBytes = 0;
            UavDesc.Buffer.FirstElement = 0;
            UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            UavDesc.Buffer.NumElements = uint32(NumElements);

            UAV = g_rhi_dx12->DescriptorHeaps.Alloc();
            g_rhi_dx12->Device->CreateUnorderedAccessView(ResourceRHI.Get(), nullptr, &UavDesc, UAV.CPUHandle);
        }
    }

    void Release()
    {
        SRV.Free();
        UAV.Free();

        ResourceRHI.Reset();
    }
};

struct jRawBufferInit
{
    static constexpr int32 Stride = 4;

    uint64 NumElements = 0;
    bool IsAllowUAV = false;
    bool IsCPUAccessible = false;
    const void* InitData = nullptr;
    D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    const wchar_t* Name = nullptr;
    ComPtr<ID3D12Resource> ResourceRHI;
    jDescriptor_DX12 SRV;
    jDescriptor_DX12 UAV;

    void Initialize()
    {
        const D3D12_RESOURCE_STATES InitialResourceState = InitData ? D3D12_RESOURCE_STATE_COMMON : InitialState;
        const uint64 AlignedSize = Align(Stride * NumElements, Stride);
        ResourceRHI = CreateDefaultResource(AlignedSize, InitialResourceState, false, IsCPUAccessible, Name);
        CopyInitialData(ResourceRHI, InitData, Stride * NumElements, Stride, IsCPUAccessible);

        // SRV 생성
        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
        SrvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Buffer.FirstElement = 0;
        SrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        SrvDesc.Buffer.NumElements = uint32(NumElements);

        SRV = g_rhi_dx12->DescriptorHeaps.Alloc();
        g_rhi_dx12->Device->CreateShaderResourceView(ResourceRHI.Get(), &SrvDesc, SRV.CPUHandle);

        // UAV 생성
        if (IsAllowUAV)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc = { };
            UavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            UavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            UavDesc.Buffer.CounterOffsetInBytes = 0;
            UavDesc.Buffer.FirstElement = 0;
            UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            UavDesc.Buffer.NumElements = uint32(NumElements);

            UAV = g_rhi_dx12->DescriptorHeaps.Alloc();
            g_rhi_dx12->Device->CreateUnorderedAccessView(ResourceRHI.Get(), nullptr, &UavDesc, UAV.CPUHandle);
        }
    }

    void Release()
    {
        SRV.Free();
        UAV.Free();

        ResourceRHI.Reset();
    }
};

namespace jBufferUtil_DX12
{

std::shared_ptr<jCreatedResource> CreateBufferInternal(uint64 InSize, uint16 InAlignment, EBufferCreateFlag InBufferCreateFlag
    , D3D12_RESOURCE_STATES InInitialState = D3D12_RESOURCE_STATE_COMMON, const wchar_t* InResourceName = nullptr);

jBuffer_DX12* CreateBuffer(uint64 InSize, uint16 InAlignment, EBufferCreateFlag InBufferCreateFlag
    , D3D12_RESOURCE_STATES InInitialState = D3D12_RESOURCE_STATE_COMMON, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr);

std::shared_ptr<jCreatedResource> CreateImageInternal(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , D3D12_RESOURCE_DIMENSION InType, DXGI_FORMAT InFormat, ETextureCreateFlag InTextureCreateFlag, EImageLayout InImageLayout = EImageLayout::UNDEFINED, D3D12_CLEAR_VALUE* InClearValue = nullptr, const wchar_t* InResourceName = nullptr);

jTexture_DX12* CreateImage(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , ETextureType InType, ETextureFormat InFormat, ETextureCreateFlag InTextureCreateFlag, EImageLayout InImageLayout = EImageLayout::UNDEFINED, const jRTClearValue& InClearValue = jRTClearValue::Invalid, const wchar_t* InResourceName = nullptr);

jTexture_DX12* CreateImage(const std::shared_ptr<jCreatedResource>& InTexture, ETextureCreateFlag InTextureCreateFlag, EImageLayout InImageLayout, const jRTClearValue& InClearValue, const wchar_t* InResourceName);

uint64 CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, uint64 InBufferOffset, ID3D12Resource* InImage, int32 InImageSubresourceIndex = 0);
uint64 CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, uint64 InBufferOffset, ID3D12Resource* InImage, int32 InNumOfImageSubresource, int32 InStartImageSubresource);
void CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, ID3D12Resource* InImage, const std::vector<jImageSubResourceData>& InSubresourceData);
void CopyBuffer(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InSrcBuffer, ID3D12Resource* InDstBuffer, uint64 InSize, uint64 InSrcOffset, uint64 InDstOffset);
void CopyBuffer(ID3D12Resource* InSrcBuffer, ID3D12Resource* InDstBuffer, uint64 InSize, uint64 InSrcOffset, uint64 InDstOffset);

void CreateConstantBufferView(jBuffer_DX12* InBuffer);
void CreateShaderResourceView(jBuffer_DX12* InBuffer);
void CreateShaderResourceView(jBuffer_DX12* InBuffer, uint32 InStride, uint32 InCount);
void CreateShaderResourceView(jBuffer_DX12* InBuffer, DXGI_FORMAT InFormat);
void CreateUnorderedAccessView(jBuffer_DX12* InBuffer);
void CreateShaderResourceView(jTexture_DX12* InTexture);
void CreateDepthStencilView(jTexture_DX12* InTexture);
void CreateUnorderedAccessView(jTexture_DX12* InTexture);
void CreateRenderTargetView(jTexture_DX12* InTexture);

}
