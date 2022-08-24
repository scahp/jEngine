#pragma once

struct jIndexBuffer_Vulkan : public jIndexBuffer
{
    std::shared_ptr<jBuffer_Vulkan> BufferPtr;

    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;

    FORCEINLINE uint32 GetIndexCount() const
    {
        return IndexStreamData->ElementCount;
    }
};

