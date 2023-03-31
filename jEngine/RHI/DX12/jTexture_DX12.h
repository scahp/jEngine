#pragma once
#include "../jTexture.h"
#include "jDescriptorHeap_DX12.h"

struct jTexture_DX12 : public jTexture
{
    jTexture_DX12() = default;
    jTexture_DX12(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight, int32 InLayerCount, EMSAASamples InSampleCount, int32 InMipLevel, bool InSRGB
        , const ComPtr<ID3D12Resource>& InImage, jDescriptor_DX12 InRTV = {}, jDescriptor_DX12 InSRV = {}, jDescriptor_DX12 InUAV = {}, EImageLayout InImageLayout = EImageLayout::UNDEFINED)
        : jTexture(InType, InFormat, InWidth, InHeight, InLayerCount, InSampleCount, InMipLevel, InSRGB), Image(InImage), RTV(InRTV), SRV(InSRV), UAV(InUAV), Layout(InImageLayout)
    {}
    virtual ~jTexture_DX12()
    {
    }
    ComPtr<ID3D12Resource> Image;
    EImageLayout Layout = EImageLayout::UNDEFINED;
    jDescriptor_DX12 SRV;
    jDescriptor_DX12 UAV;
    jDescriptor_DX12 RTV;

    virtual void* GetHandle() const override { return Image.Get(); }
    virtual void* GetViewHandle() const override { return nullptr; }
    virtual void* GetMemoryHandle() const override { return nullptr; }
    virtual void* GetSamplerStateHandle() const override { return nullptr; }
    virtual void Release() override {}
    virtual EImageLayout GetLayout() const override { return Layout; }
};
