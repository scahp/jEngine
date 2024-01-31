#pragma once
#include "jTexture.h"

struct jFrameBufferInfo
{
    jFrameBufferInfo() = default;
    jFrameBufferInfo(ETextureType textureType, ETextureFormat format, int32 width, int32 height, int32 layerCount = 1
        , bool isGenerateMipmap = false, int32 sampleCount = 1)
        : TextureType(textureType), Format(format), Width(width), Height(height), LayerCount(layerCount)
        , IsGenerateMipmap(isGenerateMipmap), SampleCount(sampleCount)
    {}

    size_t GetHash() const
    {
		return GETHASH_FROM_INSTANT_STRUCT(TextureType, Format, Width, Height, LayerCount, IsGenerateMipmap, SampleCount);
    }

    ETextureType TextureType = ETextureType::TEXTURE_2D;
    ETextureFormat Format = ETextureFormat::RGB8;
    int32 Width = 0;
    int32 Height = 0;
    int32 LayerCount = 1;
    bool IsGenerateMipmap = false;
    int32 SampleCount = 1;
};

struct jFrameBuffer : public std::enable_shared_from_this<jFrameBuffer>
{
    virtual ~jFrameBuffer() {}

    virtual jTexture* GetTexture(int32 index = 0) const { return Textures[index].get(); }
    virtual jTexture* GetTextureDepth(int32 index = 0) const { return TextureDepth.get(); }
    virtual ETextureType GetTextureType() const { return Info.TextureType; }
    virtual bool SetDepthAttachment(const std::shared_ptr<jTexture>& InDepth) { TextureDepth = InDepth; return true; }
    virtual void SetDepthMipLevel(int32 InLevel) {}

    virtual bool FBOBegin(int index = 0, bool mrt = false) const { return true; };
    virtual void End() const {}

    jFrameBufferInfo Info;
    std::vector<std::shared_ptr<jTexture> > Textures;
    std::shared_ptr<jTexture> TextureDepth;
};