﻿#pragma once
#include "jTexture.h"

struct jRenderTargetInfo
{
    constexpr jRenderTargetInfo() = default;
    constexpr jRenderTargetInfo(ETextureType textureType, ETextureFormat format, int32 width, int32 height, int32 layerCount = 1
        , bool isGenerateMipmap = false, EMSAASamples sampleCount = EMSAASamples::COUNT_1)
        : Type(textureType), Format(format), Width(width), Height(height), LayerCount(layerCount)
        , IsGenerateMipmap(isGenerateMipmap), SampleCount(sampleCount)
    {}

    size_t GetHash() const
    {
        size_t result = CityHash64((const char*)&Type, sizeof(Type));
        result = CityHash64WithSeed((const char*)&Format, sizeof(Format), result);
        result = CityHash64WithSeed((const char*)&Width, sizeof(Width), result);
        result = CityHash64WithSeed((const char*)&Height, sizeof(Height), result);
        result = CityHash64WithSeed((const char*)&LayerCount, sizeof(LayerCount), result);
        result = CityHash64WithSeed((const char*)&IsGenerateMipmap, sizeof(IsGenerateMipmap), result);
        result = CityHash64WithSeed((const char*)&SampleCount, sizeof(SampleCount), result);
        return result;
    }

    ETextureType Type = ETextureType::TEXTURE_2D;
    ETextureFormat Format = ETextureFormat::RGB8;
    int32 Width = 0;
    int32 Height = 0;
    int32 LayerCount = 1;
    bool IsGenerateMipmap = false;
    EMSAASamples SampleCount = EMSAASamples::COUNT_1;
};

struct jRenderTarget final : public std::enable_shared_from_this<jRenderTarget>
{
    // Create render target from texture, It is useful to create render target from swapchain texture
    template <typename T1, class... T2>
    static std::shared_ptr<jRenderTarget> CreateFromTexture(const T2&... args)
    {
        const auto& T1Ptr = std::make_shared<T1>(args...);
        return std::make_shared<jRenderTarget>(T1Ptr);
    }

    static std::shared_ptr<jRenderTarget> CreateFromTexture(const std::shared_ptr<jTexture>& texturePtr)
    {
        return std::make_shared<jRenderTarget>(texturePtr);
    }

    constexpr jRenderTarget() = default;
    jRenderTarget(const std::shared_ptr<jTexture>& InTexturePtr)
        : TexturePtr(InTexturePtr)
    {
        jTexture* texture = TexturePtr.get();
        if (texture)
        {
            Info.Type = texture->Type;
            Info.Format = texture->Format;
            Info.Width = texture->Width;
            Info.Height = texture->Height;
            Info.LayerCount = texture->LayerCount;
            Info.SampleCount = texture->SampleCount;
        }
    }

    ~jRenderTarget() {}

    size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = Info.GetHash();
        Hash = CityHash64WithSeed(reinterpret_cast<uint64>(GetViewHandle()), Hash);
        return Hash;
    }

    void Return();

    jTexture* GetTexture() const { return TexturePtr.get(); }
    const void* GetViewHandle() const { return TexturePtr.get() ? TexturePtr->GetViewHandle() : nullptr; }

    jRenderTargetInfo Info;
    std::shared_ptr<jTexture> TexturePtr;

    mutable size_t Hash = 0;
    bool bCreatedFromRenderTargetPool = false;
};