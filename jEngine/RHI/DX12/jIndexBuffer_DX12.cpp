﻿#include "pch.h"
#include "jIndexBuffer_DX12.h"
#include "../jRenderFrameContext.h"
#include "jBufferUtil_DX12.h"

void jIndexBuffer_DX12::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    //check(IndexStreamData->Param->Attributes.size());
    //VkIndexType IndexType = VK_INDEX_TYPE_UINT16;
    //switch (IndexStreamData->Param->Attributes[0].UnderlyingType)
    //{
    //case EBufferElementType::BYTE:
    //    IndexType = VK_INDEX_TYPE_UINT8_EXT;
    //    break;
    //case EBufferElementType::UINT16:
    //    IndexType = VK_INDEX_TYPE_UINT16;
    //    break;
    //case EBufferElementType::UINT32:
    //    IndexType = VK_INDEX_TYPE_UINT32;
    //    break;
    //case EBufferElementType::FLOAT:
    //    check(0);
    //    break;
    //default:
    //    break;
    //}
    //check(InRenderFrameContext);
    //check(InRenderFrameContext->GetActiveCommandBuffer());
    //vkCmdBindIndexBuffer((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle(), BufferPtr->Buffer, BufferPtr->Offset, IndexType);
}

void jIndexBuffer_DX12::Bind(jCommandBuffer_DX12* InCommandList)
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
    BufferPtr = std::shared_ptr<jBuffer_DX12>(
        jBufferUtil_DX12::CreateBuffer(bufferSize, 0, false, false, D3D12_RESOURCE_STATE_COMMON, InStreamData->Param->GetBufferData(), bufferSize));

    //// Create SRV of index buffer
    //auto IndexParam = InStreamData->Param;
    //jBufferUtil_DX12::CreateShaderResourceView(BufferPtr.get(), (uint32)IndexParam->GetElementSize()
    //    , (uint32)(IndexParam->GetBufferSize() / IndexParam->GetElementSize()), GetDX12TextureFormat(IndexType));

    // Create Index buffer View
    IBView.BufferLocation = BufferPtr->GetGPUAddress();
    IBView.SizeInBytes = BufferPtr->GetAllocatedSize();
    IBView.Format = IndexType;

    return true;
}