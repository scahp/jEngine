#pragma once
#include "jRHIType_DX12.h"
#include "jDescriptorHeap_DX12.h"

struct jBuffer_DX12 : public jBuffer
{
    jBuffer_DX12() = default;
    jBuffer_DX12(std::shared_ptr<jCreatedResource> InBuffer, uint64 InSize, uint16 InAlignment, EBufferCreateFlag InBufferCreateFlag = EBufferCreateFlag::NONE)
        : Buffer(InBuffer), Size(InSize), Alignment(InAlignment), BufferCreateFlag(InBufferCreateFlag)
    { }
    virtual ~jBuffer_DX12() {}
    virtual void Release() override;

    virtual void* GetMappedPointer() const override { return CPUAddress; }
    virtual void* Map(uint64 offset, uint64 size) override
    {
        check(Buffer && Buffer->IsValid());

        if (!(BufferCreateFlag & EBufferCreateFlag::CPUAccess | EBufferCreateFlag::Readback))
            return nullptr;

        if (CPUAddress)
            Unmap();

        D3D12_RANGE range = {};
        range.Begin = offset;
        range.End = offset + size;
        Buffer->Get()->Map(0, &range, reinterpret_cast<void**>(&CPUAddress));

        return CPUAddress;
    }
    virtual void* Map() override
    {
        check(Buffer && Buffer->IsValid());

        if (!(BufferCreateFlag & (EBufferCreateFlag::CPUAccess | EBufferCreateFlag::Readback)))
            return nullptr;

        if (CPUAddress)
            Unmap();

        D3D12_RANGE range = {};
        Buffer->Get()->Map(0, &range, reinterpret_cast<void**>(&CPUAddress));

        return CPUAddress;
    }
    virtual void Unmap() override
    {
        check(Buffer && Buffer->IsValid());

        if (!(BufferCreateFlag & EBufferCreateFlag::CPUAccess | EBufferCreateFlag::Readback))
            return;

        Buffer->Get()->Unmap(0, nullptr);
        CPUAddress = nullptr;
    }
    virtual void UpdateBuffer(const void* data, uint64 size) override
    {
        ensure(CPUAddress);
        if (CPUAddress)
        {
            check(Size >= size);
            memcpy(CPUAddress, data, size);
        }
    }

    virtual void* GetHandle() const override { return Buffer->Get(); }
    virtual uint64 GetAllocatedSize() const override { return (uint32)Size; }
    virtual uint64 GetBufferSize() const { return (uint32)Size; }
    virtual uint64 GetOffset() const { return Offset; }
    FORCEINLINE uint64 GetGPUAddress() const { return Buffer->GetGPUVirtualAddress(); }

    EBufferCreateFlag BufferCreateFlag = EBufferCreateFlag::NONE;
    uint64 Size = 0;
    uint16 Alignment = 0;
    uint64 Offset = 0;
    uint8* CPUAddress = nullptr;
    std::shared_ptr<jCreatedResource> Buffer;
    jDescriptor_DX12 CBV;
    jDescriptor_DX12 SRV;
    jDescriptor_DX12 UAV;
};