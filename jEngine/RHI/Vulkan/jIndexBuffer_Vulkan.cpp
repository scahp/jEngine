#include "pch.h"
#include "jIndexBuffer_Vulkan.h"
#include "../jRenderFrameContext.h"

void jIndexBuffer_Vulkan::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    VkIndexType IndexType = VK_INDEX_TYPE_UINT16;
    switch (IndexStreamData->Param->ElementType)
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
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
    vkCmdBindIndexBuffer((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), Buffer.Buffer, 0, IndexType);
}