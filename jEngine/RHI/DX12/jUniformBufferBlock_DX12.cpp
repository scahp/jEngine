#include "pch.h"
#include "jUniformBufferBlock_DX12.h"
#include "jBufferUtil_DX12.h"
#include "jRHI_DX12.h"
#include "jRingBuffer_DX12.h"

void jUniformBufferBlock_DX12::Init(size_t size)
{
    check(size);

    size = Align<uint64>(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    if (jLifeTimeType::MultiFrame == LifeType)
    {
        Buffer = jBufferUtil_DX12::CreateBuffer(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, true, false, D3D12_RESOURCE_STATE_COMMON, nullptr, 0, TEXT("UniformBufferBlock"));
        jBufferUtil_DX12::CreateConstantBufferView(Buffer);
    }
}

void jUniformBufferBlock_DX12::Release()
{
    if (Buffer)
    {
        Buffer->Release();
        delete Buffer;
    }
}

void jUniformBufferBlock_DX12::UpdateBufferData(const void* InData, size_t InSize)
{
    if (jLifeTimeType::MultiFrame == LifeType)
    {
        check(Buffer);
        check(Buffer->GetAllocatedSize() >= InSize);

        if (ensure(Buffer->Map()))
        {
            uint8* startAddr = ((uint8*)Buffer->GetMappedPointer());
            if (InData)
                memcpy(startAddr, InData, InSize);
            else
                memset(startAddr, 0, InSize);
        }
    }
    else
    {
        RingBuffer = g_rhi_dx12->GetOneFrameUniformRingBuffer();
        RingBufferAllocatedSize = Align(InSize, 256);
        RingBufferOffset = RingBuffer->Alloc(RingBufferAllocatedSize);
        RingBufferDestAddress = ((uint8*)RingBuffer->GetMappedPointer()) + RingBufferOffset;
        if (InData)
            memcpy(RingBufferDestAddress, InData, InSize);
        else
            memset(RingBufferDestAddress, 0, InSize);
    }
}

void jUniformBufferBlock_DX12::ClearBuffer(int32 clearValue)
{
    check(Buffer);
    UpdateBufferData(nullptr, Buffer->GetAllocatedSize());
}

void* jUniformBufferBlock_DX12::GetBuffer() const
{
    return (jLifeTimeType::MultiFrame == LifeType)
        ? Buffer->GetHandle() : RingBuffer->GetHandle();
}

void* jUniformBufferBlock_DX12::GetBufferMemory() const
{
    return (jLifeTimeType::MultiFrame == LifeType)
        ? Buffer->CPUAddress : RingBufferDestAddress;
}

size_t jUniformBufferBlock_DX12::GetBufferSize() const
{
    return (jLifeTimeType::MultiFrame == LifeType) ? Buffer->Size : RingBufferAllocatedSize;
}

const jDescriptor_DX12& jUniformBufferBlock_DX12::GetCBV() const
{
    if (jLifeTimeType::MultiFrame == LifeType)
    {
        return Buffer->CBV;
    }

    return RingBuffer->CBV;
}

uint64 jUniformBufferBlock_DX12::GetGPUAddress() const
{
    return (jLifeTimeType::MultiFrame == LifeType) ? Buffer->GetGPUAddress() : (RingBuffer->GetGPUAddress() + RingBufferOffset);
}
