#pragma once
#include "jTexture.h"

struct jRenderTargetInfo
{
    constexpr jRenderTargetInfo() = default;
    constexpr jRenderTargetInfo(ETextureType textureType, ETextureFormat format, int32 width, int32 height, int32 layerCount = 1
        , bool isGenerateMipmap = false, int32 sampleCount = 1)
        : Type(textureType), Format(format), Width(width), Height(height), LayerCount(layerCount)
        , IsGenerateMipmap(isGenerateMipmap), SampleCount(sampleCount)
    {}

    size_t GetHash() const
    {
        size_t result = CityHash64((const char*)&Type, sizeof(Type));
        result ^= CityHash64((const char*)&Format, sizeof(Format));
        result ^= CityHash64((const char*)&Width, sizeof(Width));
        result ^= CityHash64((const char*)&Height, sizeof(Height));
        result ^= CityHash64((const char*)&LayerCount, sizeof(LayerCount));
        result ^= CityHash64((const char*)&IsGenerateMipmap, sizeof(IsGenerateMipmap));
        result ^= CityHash64((const char*)&SampleCount, sizeof(SampleCount));
        return result;
    }

    ETextureType Type = ETextureType::TEXTURE_2D;
    ETextureFormat Format = ETextureFormat::RGB8;
    int32 Width = 0;
    int32 Height = 0;
    int32 LayerCount = 1;
    bool IsGenerateMipmap = false;
    int32 SampleCount = 1;
};

struct jRenderTarget final : public std::enable_shared_from_this<jRenderTarget>
{
    // Create render target from texture, It is useful to create render target from swapchain texture
    template <typename T1, class... T2>
    static std::shared_ptr<jRenderTarget> CreateFromTexture(const T2&... args)
    {
        const auto& T1Ptr = std::shared_ptr<T1>(new T1(args...));
        return std::shared_ptr<jRenderTarget>(new jRenderTarget(T1Ptr));
    }

    static std::shared_ptr<jRenderTarget> CreateFromTexture(const std::shared_ptr<jTexture>& texturePtr)
    {
        return std::shared_ptr<jRenderTarget>(new jRenderTarget(texturePtr));
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

    ~jRenderTarget()
    {}

    size_t GetHash() const
    {
        if (Hash)
            return Hash;

        Hash = Info.GetHash();
        Hash ^= reinterpret_cast<uint64>(GetViewHandle());
        return Hash;
    }

    jTexture* GetTexture() const { return TexturePtr.get(); }
    const void* GetViewHandle() const { return TexturePtr.get() ? TexturePtr->GetViewHandle() : nullptr; }

    jRenderTargetInfo Info;
    std::shared_ptr<jTexture> TexturePtr;

    mutable size_t Hash = 0;
};