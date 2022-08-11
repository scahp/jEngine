#pragma once

struct jIndexBuffer_Vulkan : public jIndexBuffer
{
    jBuffer_Vulkan Buffer;

    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;

    FORCEINLINE uint32 GetIndexCount() const
    {
        return IndexStreamData->ElementCount;
    }
};

