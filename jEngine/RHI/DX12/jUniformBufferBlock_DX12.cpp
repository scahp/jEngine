#include "pch.h"
#include "jUniformBufferBlock_DX12.h"
#include "jBufferUtil_DX12.h"
#include "jRHI_DX12.h"
#include "jRingBuffer_DX12.h"

jUniformBufferBlock_DX12::~jUniformBufferBlock_DX12()
{
    Release();
}

void jUniformBufferBlock_DX12::Init(size_t size)
{
    check(size);

    size = Align<uint64>(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    if (jLifeTimeType::MultiFrame == LifeType)
    {
        BufferPtr = jBufferUtil_DX12::CreateBuffer(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, EBufferCreateFlag::CPUAccess, EResourceLayout::GENERAL, nullptr, 0, TEXT("UniformBufferBlock"));
        jBufferUtil_DX12::CreateConstantBufferView(BufferPtr.get());
    }
}

void jUniformBufferBlock_DX12::Release()
{
}

void jUniformBufferBlock_DX12::UpdateBufferData(const void* InData, size_t InSize)
{
    if (jLifeTimeType::MultiFrame == LifeType)
    {
        check(BufferPtr);
        check(BufferPtr->GetAllocatedSize() >= InSize);

        if (ensure(BufferPtr->Map()))
        {
            uint8* startAddr = ((uint8*)BufferPtr->GetMappedPointer());
            if (InData)
                memcpy(startAddr, InData, InSize);
            else
                memset(startAddr, 0, InSize);
            BufferPtr->Unmap();
        }
    }
    else
    {
        RingBuffer = g_rhi_dx12->GetOneFrameUniformRingBuffer();
        RingBufferAllocatedSize = Align(InSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
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
    check(BufferPtr);
    UpdateBufferData(nullptr, BufferPtr->GetAllocatedSize());
}

void* jUniformBufferBlock_DX12::GetLowLevelResource() const
{
    return (jLifeTimeType::MultiFrame == LifeType)
        ? BufferPtr->GetHandle() : RingBuffer->GetHandle();
}

void* jUniformBufferBlock_DX12::GetLowLevelMemory() const
{
    return (jLifeTimeType::MultiFrame == LifeType)
        ? BufferPtr->CPUAddress : RingBufferDestAddress;
}

size_t jUniformBufferBlock_DX12::GetBufferSize() const
{
    return (jLifeTimeType::MultiFrame == LifeType) ? BufferPtr->Size : RingBufferAllocatedSize;
}

const jDescriptor_DX12& jUniformBufferBlock_DX12::GetCBV() const
{
    if (jLifeTimeType::MultiFrame == LifeType)
    {
        return BufferPtr->CBV;
    }

    return RingBuffer->CBV;
}

uint64 jUniformBufferBlock_DX12::GetGPUAddress() const
{
    return (jLifeTimeType::MultiFrame == LifeType) ? BufferPtr->GetGPUAddress() : (RingBuffer->GetGPUAddress() + RingBufferOffset);
}
