#include "pch.h"
#include "jIndexBuffer_DX12.h"
#include "../jRenderFrameContext.h"
#include "jBufferUtil_DX12.h"

void jIndexBuffer_DX12::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    auto CommandBuffer_DX12 = (jCommandBuffer_DX12*)InRenderFrameContext->GetActiveCommandBuffer();
    check(CommandBuffer_DX12);

    Bind(CommandBuffer_DX12);
}

void jIndexBuffer_DX12::Bind(jCommandBuffer_DX12* InCommandList) const
{
    check(InCommandList->CommandList);
    InCommandList->CommandList->IASetIndexBuffer(&IBView);
}

bool jIndexBuffer_DX12::Initialize(const std::shared_ptr<jIndexStreamData>& InStreamData)
{
    if (!InStreamData)
        return false;

    check(InStreamData);
    check(InStreamData->Param);
    IndexStreamData = InStreamData;

    const size_t bufferSize = InStreamData->Param->GetBufferSize();

    DXGI_FORMAT IndexType = DXGI_FORMAT_R16_UINT;
    switch (IndexStreamData->Param->Attributes[0].UnderlyingType)
    {
    case EBufferElementType::BYTE:
        IndexType = DXGI_FORMAT_R8_UINT;
        break;
    case EBufferElementType::UINT16:
        IndexType = DXGI_FORMAT_R16_UINT;
        break;
    case EBufferElementType::UINT32:
        IndexType = DXGI_FORMAT_R32_UINT;
        break;
    case EBufferElementType::FLOAT:
        check(0);
        break;
    default:
        break;
    }

    // Create index buffer
    BufferPtr = g_rhi->CreateFormattedBuffer<jBuffer_DX12>(bufferSize, 0, GetDX12TextureFormat(IndexType), EBufferCreateFlag::UAV
        , EResourceLayout::GENERAL, InStreamData->Param->GetBufferData(), bufferSize, TEXT("IndexBuffer"));

    // Create Index buffer View
    IBView.BufferLocation = BufferPtr->GetGPUAddress();
    IBView.SizeInBytes = (uint32)BufferPtr->GetAllocatedSize();
    IBView.Format = IndexType;

    return true;
}
