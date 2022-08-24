#pragma once
#include "../jRHI.h"

struct jShaderStorageBufferObject_Vulkan : public IShaderStorageBufferObject
{
    using IShaderStorageBufferObject::IShaderStorageBufferObject;
    using IShaderStorageBufferObject::UpdateBufferData;
    virtual ~jShaderStorageBufferObject_Vulkan()
    {
        Destroy();
    }

    void Destroy();

    virtual void Init(size_t size) override;
    virtual void UpdateBufferData(const void* InData, size_t InSize) override;

    virtual void ClearBuffer(int32 clearValue) override;

    virtual void* GetBuffer() const override { return BufferPtr->Buffer; }
    virtual void* GetBufferMemory() const override { return BufferPtr->BufferMemory; }

    virtual size_t GetBufferSize() const override { return BufferPtr->AllocatedSize; }
    virtual size_t GetBufferOffset() const override { return BufferPtr->Offset; }

    std::shared_ptr<jBuffer_Vulkan> BufferPtr;
};