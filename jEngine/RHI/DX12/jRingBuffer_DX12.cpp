#include "pch.h"
#include "jRingBuffer_DX12.h"

void jRingBuffer_DX12::Create(uint64 totalSize, uint32 alignment /*= 16*/)
{
    jScopedLock s(&Lock);

    Release();

    RingBufferSize = Align(totalSize, (uint64)alignment);
    RingBufferOffset = 0;
    Alignment = alignment;

    D3D12_RESOURCE_DESC desc = { };
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = uint32(RingBufferSize);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Alignment = 0;

    check(g_rhi_dx12);
    Buffer = g_rhi_dx12->CreateUploadResource(&desc, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!ensure(Buffer))
        return;

    {
        check(!CBV.IsValid());
        CBV = g_rhi_dx12->DescriptorHeaps.Alloc();

        D3D12_CONSTANT_BUFFER_VIEW_DESC Desc;
        Desc.BufferLocation = Buffer->GetGPUVirtualAddress();
        Desc.SizeInBytes = (uint32)RingBufferSize;

        g_rhi_dx12->Device->CreateConstantBufferView(&Desc, CBV.CPUHandle);
    }

    D3D12_RANGE readRange = { };
    if (JFAIL(Buffer->Map(0, &readRange, reinterpret_cast<void**>(&MappedPointer))))
    {
        return;
    }
}
