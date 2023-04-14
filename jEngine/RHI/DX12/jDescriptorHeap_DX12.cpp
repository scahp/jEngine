#include "pch.h"
#include "jDescriptorHeap_DX12.h"
#include "jRingBuffer_DX12.h"

void jDescriptorHeap_DX12::Initialize(EDescriptorHeapTypeDX12 InHeapType, bool InShaderVisible, uint32 InNumOfDescriptors)
{
    jScopedLock s(&DescriptorLock);

    check(!InShaderVisible || (InShaderVisible && (InHeapType != EDescriptorHeapTypeDX12::RTV && InHeapType != EDescriptorHeapTypeDX12::DSV)));

    const auto HeapTypeDX12 = GetDX12DescriptorHeapType(InHeapType);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = { };
    heapDesc.NumDescriptors = InNumOfDescriptors;
    heapDesc.Type = HeapTypeDX12;
    heapDesc.Flags = InShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);
    if (JFAIL(g_rhi_dx12->Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&Heap))))
        return;

    CPUHandleStart = Heap->GetCPUDescriptorHandleForHeapStart();
    if (InShaderVisible)
        GPUHandleStart = Heap->GetGPUDescriptorHandleForHeapStart();
    else
        GPUHandleStart.ptr = (UINT64)-1;

    DescriptorSize = g_rhi_dx12->Device->GetDescriptorHandleIncrementSize(HeapTypeDX12);
    NumOfDescriptors = InNumOfDescriptors;

    for (uint32 i = 0; i < NumOfDescriptors; ++i)
    {
        Pools.insert(i);
    }
}

void jDescriptorHeap_DX12::Release()
{
    jScopedLock s(&DescriptorLock);

    Heap->Release();
    CPUHandleStart = { };
    GPUHandleStart = { };
    DescriptorSize = 0;
    NumOfDescriptors = 0;
    Pools.clear();
}

void jDescriptorHeap_DX12::Free(uint32 InIndex, uint32 InDelayFrames)
{
    PendingFree.push_back({ .DescriptorIndex = InIndex, .FrameIndex = g_rhi_dx12->GetCurrentFrameIndex() });

    const int32 CurrentFrameNumber = g_rhi->GetCurrentFrameNumber();
    const int32 OldestFrameToKeep = CurrentFrameNumber - NumOfFramesToWaitBeforeReleasing;

    // ProcessPendingDescriptorPoolFree
    {
        // Check it is too early
        if (CurrentFrameNumber >= CanReleasePendingFreeShaderBindingInstanceFrameNumber)
        {
            // Release pending memory
            int32 i = 0;
            for (; i < PendingFree.size(); ++i)
            {
                PendingForFree& PendingFreeInstance = PendingFree[i];
                if (PendingFreeInstance.FrameIndex < OldestFrameToKeep)
                {
                    Pools.insert(PendingFreeInstance.DescriptorIndex);
                }
                else
                {
                    CanReleasePendingFreeShaderBindingInstanceFrameNumber = PendingFreeInstance.FrameIndex + NumOfFramesToWaitBeforeReleasing + 1;
                    break;
                }
            }
            if (i > 0)
            {
                const size_t RemainingSize = (PendingFree.size() - i);
                if (RemainingSize > 0)
                {
                    memcpy(&PendingFree[0], &PendingFree[i], sizeof(PendingForFree) * RemainingSize);
                }
                PendingFree.resize(RemainingSize);
            }
        }
    }
}

jDescriptor_DX12 jDescriptorHeap_DX12::OneFrameCreateConstantBufferView(jRingBuffer_DX12* InBuffer, uint64 InOffset, uint32 InSize, ETextureFormat InFormat)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    jDescriptor_DX12 Descriptor = OneFrameAlloc();

    check(InBuffer);
    check(InBuffer->Buffer);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = { };
    cbvDesc.BufferLocation = InBuffer->GetGPUAddress() + InOffset;
    cbvDesc.SizeInBytes = uint32(Align(InSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
    g_rhi_dx12->Device->CreateConstantBufferView(&cbvDesc, Descriptor.CPUHandle);
    return Descriptor;
}

jDescriptor_DX12 jDescriptorHeap_DX12::OneFrameCreateShaderResourceView(jRingBuffer_DX12* InBuffer, uint64 InOffset, uint32 InStride, uint32 InNumOfElement, ETextureFormat InFormat)
{
    check(g_rhi_dx12);
    check(g_rhi_dx12->Device);

    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = (InFormat == ETextureFormat::MAX) ? DXGI_FORMAT_UNKNOWN : GetDX12TextureFormat(InFormat);
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = uint32(InOffset / InStride);
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    desc.Buffer.NumElements = InNumOfElement;
    desc.Buffer.StructureByteStride = InStride;

    jDescriptor_DX12 Descriptor = OneFrameAlloc();

    check(InBuffer);
    check(InBuffer->Buffer);
    g_rhi_dx12->Device->CreateShaderResourceView(InBuffer->Buffer.Get(), &desc, Descriptor.CPUHandle);
    return Descriptor;
}
