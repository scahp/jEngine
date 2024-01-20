#include "pch.h"
#include "jIndexBuffer_Vulkan.h"
#include "../jRenderFrameContext.h"

void jIndexBuffer_Vulkan::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    check(IndexStreamData->Param->Attributes.size());

    check(InRenderFrameContext);
    check(InRenderFrameContext->GetActiveCommandBuffer());
    const VkIndexType IndexType = GetVulkanIndexFormat(IndexStreamData->Param->Attributes[0].UnderlyingType);
    vkCmdBindIndexBuffer((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle()
        , GetBuffer()->Buffer, GetBuffer()->Offset, IndexType);
}

VkIndexType jIndexBuffer_Vulkan::GetVulkanIndexFormat(EBufferElementType InType) const
{
    VkIndexType IndexType = VK_INDEX_TYPE_UINT32;
    switch (InType)
    {
    case EBufferElementType::BYTE:
        IndexType = VK_INDEX_TYPE_UINT8_EXT;
        break;
    case EBufferElementType::UINT16:
        IndexType = VK_INDEX_TYPE_UINT16;
        break;
    case EBufferElementType::UINT32:
        IndexType = VK_INDEX_TYPE_UINT32;
        break;
    case EBufferElementType::FLOAT:
        check(0);
        break;
    default:
        break;
    }
    return IndexType;
}

uint32 jIndexBuffer_Vulkan::GetVulkanIndexStride(EBufferElementType InType) const
{
    switch (InType)
    {
    case EBufferElementType::BYTE:
        return 1;
    case EBufferElementType::UINT16:
        return 2;
    case EBufferElementType::UINT32:
        return 4;
    case EBufferElementType::FLOAT:
        check(0);
        break;
    default:
        break;
    }
    check(0);
    return 4;
}

bool jIndexBuffer_Vulkan::Initialize(const std::shared_ptr<jIndexStreamData>& InStreamData)
{
    if (!InStreamData)
        return false;

    IndexStreamData = InStreamData;
    VkDeviceSize bufferSize = InStreamData->Param->GetBufferSize();

    const EBufferCreateFlag BufferCreateFlag = EBufferCreateFlag::IndexBuffer | EBufferCreateFlag::UAV | EBufferCreateFlag::AccelerationStructureBuildInput;
    BufferPtr = g_rhi->CreateStructuredBuffer<jBuffer_Vulkan>(bufferSize, 0, GetVulkanIndexStride(IndexStreamData->Param->Attributes[0].UnderlyingType)
        , BufferCreateFlag, EImageLayout::TRANSFER_DST, InStreamData->Param->GetBufferData(), bufferSize, TEXT("IndexBuffer"));

    return true;
}
