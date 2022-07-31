#include "pch.h"
#include "jVertexBuffer_Vulkan.h"

void jVertexBuffer_Vulkan::Bind() const
{
    check(g_rhi_vk->CurrentCommandBuffer);
    vkCmdBindVertexBuffers((VkCommandBuffer)g_rhi_vk->CurrentCommandBuffer->GetHandle(), 0, (uint32)BindInfos.Buffers.size(), &BindInfos.Buffers[0], &BindInfos.Offsets[0]);
}