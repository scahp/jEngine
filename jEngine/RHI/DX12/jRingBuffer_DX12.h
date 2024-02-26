#pragma once
#include "../jRHIType.h"

struct jRingBuffer_DX12 : public jBuffer
{
    jRingBuffer_DX12() = default;
    virtual ~jRingBuffer_DX12()
    {
        Release();
    }

    virtual void Create(uint64 totalSize, uint32 alignment = 16);
    virtual void Reset();
    virtual uint64 Alloc(uint64 allocSize)
    {
        jScopedLock s(&Lock);

        const uint64 allocOffset = Align<uint64>(RingBufferOffset, Alignment);
        if (allocOffset + allocSize <= RingBufferSize)
        {
            RingBufferOffset = allocOffset + allocSize;
            return allocOffset;
        }

        check(0);

        return 0;
    }

    virtual uint64 AllocWithCBV(uint64 allocSize, jDescriptor_DX12& OutCBV);

    virtual void Release() override
    {
        CBV.Free();
    }

    virtual void* GetMappedPointer() const override { return MappedPointer; }
    virtual void* Map(uint64 offset, uint64 size) override
    {
        check(size);
        check(offset + size <= RingBufferSize);
        check(!MappedPointer);

        const D3D12_RANGE readRange = { .Begin = offset, .End = offset + size };
        if (JFAIL(Buffer->Resource->Map(0, &readRange, reinterpret_cast<void**>(&MappedPointer))))
        {
            return nullptr;
        }
        return MappedPointer;
    }
    virtual void* Map() override
    {
        check(RingBufferSize);
        check(!MappedPointer);
        D3D12_RANGE readRange = { };
        if (JFAIL(Buffer->Resource->Map(0, &readRange, reinterpret_cast<void**>(&MappedPointer))))
        {
            return nullptr;
        }
        return MappedPointer;
    }
    virtual void Unmap() override
    {
        check(MappedPointer);
        Buffer->Resource->Unmap(0, nullptr);
        MappedPointer = nullptr;
    }
    virtual void UpdateBuffer(const void* data, uint64 size) override
    {
        check(size <= RingBufferSize);

        if (ensure(Map(0, size)))
        {
            memcpy(MappedPointer, data, size);
            Unmap();
        }
    }

    virtual void* GetHandle() const override { return Buffer->Get(); }
    virtual uint64 GetAllocatedSize() const override { return RingBufferSize; }
    virtual uint64 GetBufferSize() const override { return RingBufferSize; }
    virtual uint64 GetOffset() const override { return RingBufferOffset; }
    FORCEINLINE uint64 GetGPUAddress() const { return Buffer->GetGPUVirtualAddress(); }

    uint64 RingBufferOffset = 0;
    uint32 Alignment = 16;
    std::shared_ptr<jCreatedResource> Buffer;
    uint64 RingBufferSize = 0;
    void* MappedPointer = nullptr;
    std::vector<jDescriptor_DX12> AllocatedCBVs;

    jDescriptor_DX12 CBV;

    jMutexLock Lock;
};
