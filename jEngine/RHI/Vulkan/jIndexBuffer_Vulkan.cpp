#include "pch.h"
#include "jIndexBuffer_Vulkan.h"
#include "../jRenderFrameContext.h"

void jIndexBuffer_Vulkan::Bind(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext) const
{
    check(IndexStreamData->Param->Attributes.size());
    VkIndexType IndexType = VK_INDEX_TYPE_UINT16;
    switch (IndexStreamData->Param->Attributes[0].UnderlyingType)
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
    check(InRenderFrameContext->GetActiveCommandBuffer());
    vkCmdBindIndexBuffer((VkCommandBuffer)InRenderFrameContext->GetActiveCommandBuffer()->GetHandle()
        , BufferPtr->Buffer, BufferPtr->Offset, IndexType);
}

bool jIndexBuffer_Vulkan::Initialize(const std::shared_ptr<jIndexStreamData>& InStreamData)
{
    if (!InStreamData)
        return false;

    IndexStreamData = InStreamData;
    VkDeviceSize bufferSize = InStreamData->Param->GetBufferSize();

    jBuffer_Vulkan stagingBuffer;
    jBufferUtil_Vulkan::AllocateBuffer(EVulkanBufferBits::TRANSFER_SRC
        , EVulkanMemoryBits::HOST_VISIBLE | EVulkanMemoryBits::HOST_COHERENT, bufferSize, stagingBuffer);

    stagingBuffer.UpdateBuffer(InStreamData->Param->GetBufferData(), bufferSize);

    BufferPtr = std::make_shared<jBuffer_Vulkan>();
    jBufferUtil_Vulkan::AllocateBuffer(
        EVulkanBufferBits::TRANSFER_DST | EVulkanBufferBits::INDEX_BUFFER
        | EVulkanBufferBits::SHADER_DEVICE_ADDRESS | EVulkanBufferBits::ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY
        | EVulkanBufferBits::STORAGE_BUFFER
        , EVulkanMemoryBits::DEVICE_LOCAL, bufferSize, *BufferPtr.get());
    jBufferUtil_Vulkan::CopyBuffer(stagingBuffer, *BufferPtr.get(), bufferSize);

    stagingBuffer.Release();
    return true;
}
