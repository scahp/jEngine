#pragma once
#include "jRHIType_DX12.h"
#include "../jTexture.h"
#include "jDescriptorHeap_DX12.h"

struct jTexture_DX12 : public jTexture
{
    jTexture_DX12() = default;
    jTexture_DX12(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight, int32 InLayerCount, EMSAASamples InSampleCount, int32 InMipLevel, bool InSRGB, const jRTClearValue& InClearValue
        , const std::shared_ptr<jCreatedResource>& InImage, jDescriptor_DX12 InRTV = {}, jDescriptor_DX12 InDSV = {}, jDescriptor_DX12 InSRV = {}, jDescriptor_DX12 InUAV = {}, EImageLayout InImageLayout = EImageLayout::UNDEFINED)
        : jTexture(InType, InFormat, InWidth, InHeight, InLayerCount, InSampleCount, InMipLevel, InSRGB), Image(InImage), RTV(InRTV), DSV(InDSV), SRV(InSRV), UAV(InUAV), Layout(InImageLayout)
    {}
    virtual ~jTexture_DX12();

    std::shared_ptr<jCreatedResource> Image;
    EImageLayout Layout = EImageLayout::UNDEFINED;
    jDescriptor_DX12 SRV;
    jDescriptor_DX12 UAV;
    jDescriptor_DX12 RTV;
    jDescriptor_DX12 DSV;

    std::map<int32, jDescriptor_DX12> UAVMipMap;

    virtual void* GetHandle() const override { return Image->Get(); }
    virtual void* GetSamplerStateHandle() const override { return nullptr; }
    virtual void Release() override;
    virtual EImageLayout GetLayout() const override { return Layout; }
};
