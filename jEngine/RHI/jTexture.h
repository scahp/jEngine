#pragma once

struct jTexture
{
    constexpr jTexture() = default;
    constexpr jTexture(ETextureType InType, ETextureFormat InFormat, int32 InWidth, int32 InHeight
        , int32 InLayerCount = 1, EMSAASamples InSampleCount = EMSAASamples::COUNT_1, int32 InMipLevel = 1, bool InSRGB = false)
        : Type(InType), Format(InFormat), Width(InWidth), Height(InHeight), LayerCount(InLayerCount)
        , SampleCount(InSampleCount), MipLevel(InMipLevel), sRGB(InSRGB)
    {}
    virtual ~jTexture() {}
    static int32 GetMipLevels(int32 InWidth, int32 InHeight)
    {
        return 1 + (int32)floorf(log2f(fmaxf((float)InWidth, (float)InHeight)));
    }

    virtual void* GetHandle() const { return nullptr; }
    virtual void* GetViewHandle() const { return nullptr; }
    virtual void* GetMemoryHandle() const { return nullptr; }
    virtual void* GetSamplerStateHandle() const { return nullptr; }
    virtual void Release() {}
    virtual EImageLayout GetLayout() const { return EImageLayout::UNDEFINED; }

    FORCEINLINE bool IsDepthFormat() const { return ::IsDepthFormat(Format); }
    FORCEINLINE bool IsDepthOnlyFormat() const { return ::IsDepthOnlyFormat(Format); }

    ETextureType Type = ETextureType::MAX;
    ETextureFormat Format = ETextureFormat::RGB8;
    int32 LayerCount = 1;
    EMSAASamples SampleCount = EMSAASamples::COUNT_1;
    int32 MipLevel = 1;

    int32 Width = 0;
    int32 Height = 0;

    bool sRGB = false;
};
