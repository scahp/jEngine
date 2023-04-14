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
    virtual void Reset()
    {
        jScopedLock s(&Lock);

        RingBufferOffset = 0;
    }
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

    virtual void Release() override
    {
        Buffer = nullptr;
    }

    virtual void* GetMappedPointer() const override { return MappedPointer; }
    virtual void* Map(uint64 offset, uint64 size) override
    {
        check(size);
        check(offset + size <= RingBufferSize);
        check(!MappedPointer);

        const D3D12_RANGE readRange = { .Begin = offset, .End = offset + size };
        if (JFAIL(Buffer->Map(0, &readRange, reinterpret_cast<void**>(&MappedPointer))))
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
        if (JFAIL(Buffer->Map(0, &readRange, reinterpret_cast<void**>(&MappedPointer))))
        {
            return nullptr;
        }
        return MappedPointer;
    }
    virtual void Unmap() override
    {
        check(MappedPointer);
        Buffer->Unmap(0, nullptr);
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

    virtual void* GetHandle() const override { return Buffer.Get(); }
    virtual void* GetMemoryHandle() const override { return Buffer.Get(); }
    virtual uint32 GetAllocatedSize() const override { return (uint32)RingBufferSize; }
    FORCEINLINE uint64 GetGPUAddress() const { return Buffer->GetGPUVirtualAddress(); }

    uint64 RingBufferOffset = 0;
    uint32 Alignment = 16;
    ComPtr<ID3D12Resource> Buffer;
    uint64 RingBufferSize = 0;
    void* MappedPointer = nullptr;

    jMutexLock Lock;
};
