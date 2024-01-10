#pragma once
#include "../jBuffer.h"

struct jBuffer_Vulkan;

struct jIndexBuffer_Vulkan : public jIndexBuffer
{
    std::shared_ptr<jBuffer_Vulkan> BufferPtr;

    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;
    virtual bool Initialize(const std::shared_ptr<jIndexStreamData>& InStreamData) override;

    FORCEINLINE uint32 GetIndexCount() const
    {
        return IndexStreamData->ElementCount;
    }

    virtual jBuffer* GetBuffer() const override { return BufferPtr.get(); }
};

