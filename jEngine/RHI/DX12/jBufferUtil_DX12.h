#pragma once
#include "jRHIType_DX12.h"
#include "jDescriptorHeap_DX12.h"

struct jBuffer_DX12;
struct jTexture_DX12;

namespace jBufferUtil_DX12
{

ComPtr<ID3D12Resource> CreateBufferInternal(uint64 InSize, uint16 InAlignment, bool InIsCPUAccess, bool InAllowUAV
    , D3D12_RESOURCE_STATES InInitialState = D3D12_RESOURCE_STATE_COMMON, const wchar_t* InResourceName = nullptr);

jBuffer_DX12* CreateBuffer(uint64 InSize, uint16 InAlignment, bool InIsCPUAccess, bool InAllowUAV
    , D3D12_RESOURCE_STATES InInitialState = D3D12_RESOURCE_STATE_COMMON, const void* InData = nullptr, uint64 InDataSize = 0, const wchar_t* InResourceName = nullptr);

ComPtr<ID3D12Resource> CreateImageInternal(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , D3D12_RESOURCE_DIMENSION InType, DXGI_FORMAT InFormat, bool InIsRTV, bool InIsUAV, D3D12_RESOURCE_STATES InResourceState = D3D12_RESOURCE_STATE_COMMON, const wchar_t* InResourceName = nullptr);

jTexture_DX12* CreateImage(uint32 InWidth, uint32 InHeight, uint32 InArrayLayers, uint32 InMipLevels, uint32 InNumOfSample
    , ETextureType InType, ETextureFormat InFormat, bool InIsRTV, bool InIsUAV, D3D12_RESOURCE_STATES InResourceState = D3D12_RESOURCE_STATE_COMMON, const wchar_t* InResourceName = nullptr);

void CopyBufferToImage(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InBuffer, uint64 InBufferOffset, ID3D12Resource* InImage, uint32 InSize);
void CopyBuffer(ID3D12GraphicsCommandList4* InCommandBuffer, ID3D12Resource* InSrcBuffer, ID3D12Resource* InDstBuffer, uint64 InSize, uint64 InSrcOffset, uint64 InDstOffset);
void CopyBuffer(ID3D12Resource* InSrcBuffer, ID3D12Resource* InDstBuffer, uint64 InSize, uint64 InSrcOffset, uint64 InDstOffset);

void CreateConstantBufferView(jBuffer_DX12* InBuffer);
void CreateShaderResourceView(jBuffer_DX12* InBuffer);
void CreateShaderResourceView(jBuffer_DX12* InBuffer, uint32 InStride, uint32 InCount, ETextureFormat InFormat = ETextureFormat::MAX);
void CreateUnorderedAccessView(jBuffer_DX12* InBuffer);
void CreateShaderResourceView(jTexture_DX12* InTexture);
void CreateUnorderedAccessView(jTexture_DX12* InTexture);
void CreateRenderTargetView(jTexture_DX12* InTexture);

}
