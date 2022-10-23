#pragma once
#include "../jRHI.h"

struct jShaderStorageBufferObject_Vulkan : public IShaderStorageBufferObject
{
    using IShaderStorageBufferObject::IShaderStorageBufferObject;
    using IShaderStorageBufferObject::UpdateBufferData;
    virtual ~jShaderStorageBufferObject_Vulkan()
    {
    }

    virtual void Init(size_t size) override;
    virtual void UpdateBufferData(const void* InData, size_t InSize) override;

    virtual void ClearBuffer(int32 clearValue) override;

    virtual void* GetBuffer() const override { return Buffer.Buffer; }
    virtual void* GetBufferMemory() const override { return Buffer.BufferMemory; }

    virtual size_t GetBufferSize() const override { return Buffer.AllocatedSize; }
    virtual size_t GetBufferOffset() const override { return Buffer.Offset; }

    jBuffer_Vulkan Buffer;

private:
    jShaderStorageBufferObject_Vulkan(const jShaderStorageBufferObject_Vulkan&) = delete;
    jShaderStorageBufferObject_Vulkan& operator=(const jShaderStorageBufferObject_Vulkan&) = delete;
};
