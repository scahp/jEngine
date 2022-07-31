#pragma once

struct jIndexBuffer_Vulkan : public jIndexBuffer
{
    VkBuffer IndexBuffer = nullptr;
    VkDeviceMemory IndexBufferMemory = nullptr;

    virtual void Bind(const jShader* shader) const override {}
    virtual void Bind() const override;

    FORCEINLINE uint32 GetIndexCount() const
    {
        return IndexStreamData->ElementCount;
    }
};

