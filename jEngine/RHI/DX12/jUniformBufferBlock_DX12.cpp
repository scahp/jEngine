#include "pch.h"
#include "jUniformBufferBlock_DX12.h"
#include "jBufferUtil_DX12.h"
#include "jRHI_DX12.h"

void jUniformBufferBlock_DX12::Init(size_t size)
{
    check(size);

    size = Align<uint64>(size, g_rhi_vk->DeviceProperties.limits.minUniformBufferOffsetAlignment);

    Buffer = jBufferUtil_DX12::CreateBuffer(size, 0, true, false);
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
    check(Buffer);
    check(Buffer->GetAllocatedSize() >= InSize);

    if (ensure(Buffer->GetMappedPointer()))
    {
        uint8* startAddr = ((uint8*)Buffer->GetMappedPointer());
        if (InData)
            memcpy(startAddr, InData, InSize);
        else
            memset(startAddr, 0, InSize);
    }
}

void jUniformBufferBlock_DX12::ClearBuffer(int32 clearValue)
{
    check(Buffer);

    UpdateBufferData(nullptr, Buffer->GetAllocatedSize());
}
