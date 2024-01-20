#pragma once
#include "../jBuffer.h"

struct jBuffer_Vulkan;

struct jIndexBuffer_Vulkan : public jIndexBuffer
{
    std::shared_ptr<jBuffer_Vulkan> BufferPtr;

    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const override;

    VkIndexType GetVulkanIndexFormat(EBufferElementType IndexType) const;
    uint32 GetVulkanIndexStride(EBufferElementType IndexType) const;

    virtual bool Initialize(const std::shared_ptr<jIndexStreamData>& InStreamData) override;

    FORCEINLINE uint32 GetIndexCount() const
    {
        return IndexStreamData->ElementCount;
    }

    virtual jBuffer_Vulkan* GetBuffer() const override { return BufferPtr.get(); }
};

