#include "pch.h"
#include "jVertexBuffer_Vulkan.h"
#include "../jRenderFrameContext.h"

void jVertexBuffer_Vulkan::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    check(InRenderFrameContext);
    check(InRenderFrameContext->CommandBuffer);
    vkCmdBindVertexBuffers((VkCommandBuffer)InRenderFrameContext->CommandBuffer->GetHandle(), BindInfos.StartBindingIndex, (uint32)BindInfos.Buffers.size(), &BindInfos.Buffers[0], &BindInfos.Offsets[0]);
}