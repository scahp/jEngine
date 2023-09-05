#pragma once
#include "jBuffer_DX12.h"

struct jUniformBufferBlock_DX12 : public IUniformBufferBlock
{
    using IUniformBufferBlock::IUniformBufferBlock;
    using IUniformBufferBlock::UpdateBufferData;
    virtual ~jUniformBufferBlock_DX12()
    {}

    virtual void Init(size_t size) override;

    virtual void Release() override;
    virtual void UpdateBufferData(const void* InData, size_t InSize) override;

    virtual void ClearBuffer(int32 clearValue) override;

    virtual void* GetBuffer() const override;
    virtual void* GetBufferMemory() const override;

    virtual size_t GetBufferSize() const override;
    virtual size_t GetBufferOffset() const override { return 0; }

    const jDescriptor_DX12& GetCBV() const;

private:
    jUniformBufferBlock_DX12(const jUniformBufferBlock_DX12&) = delete;
    jUniformBufferBlock_DX12& operator=(const jUniformBufferBlock_DX12&) = delete;

    jBuffer_DX12* Buffer = nullptr;

    jRingBuffer_DX12* RingBuffer = nullptr;
    uint8* RingBufferDestAddress = nullptr;
    size_t RingBufferAllocatedSize = 0;
};
