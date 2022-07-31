#include "pch.h"
#include "jIndexBuffer_Vulkan.h"

void jIndexBuffer_Vulkan::Bind() const
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
    check(g_rhi_vk->CurrentCommandBuffer);
    vkCmdBindIndexBuffer((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), Buffer.Buffer, 0, IndexType);
}