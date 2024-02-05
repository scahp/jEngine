#pragma once
#include "RHI/jResourceBarrierBatcher.h"

struct jBarrier_Vulkan
{
    VkPipelineStageFlags SrcStage = 0;
    VkPipelineStageFlags DstStage = 0;

    union
    {
        VkBufferMemoryBarrier BufferBarrier;
        VkImageMemoryBarrier ImageBarrier;
        VkMemoryBarrier MemroyBarrier;
    } Barrier;

    // This functions rely on first element of the Barriers would be VkStructureType
    FORCEINLINE bool IsBufferBarrier() const { return Barrier.BufferBarrier.sType == VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; }
    FORCEINLINE bool IsImageBarrier() const { return Barrier.BufferBarrier.sType == VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; }
    FORCEINLINE bool IsMemoryBarrier() const { return Barrier.BufferBarrier.sType == VK_STRUCTURE_TYPE_MEMORY_BARRIER; }
};

class jResourceBarrierBatcher_Vulkan : public jResourceBarrierBatcher
{
public:
    virtual ~jResourceBarrierBatcher_Vulkan() {}

    virtual void AddUAV(jBuffer* InBuffer) override;
    virtual void AddUAV(jTexture* InTexture) override;

    virtual void AddTransition(jBuffer* InBuffer, EResourceLayout InNewLayout) override;
    virtual void AddTransition(jTexture* InTexture, EResourceLayout InNewLayout) override;

    virtual void Flush(jCommandBuffer* InCommandBuffer) override;

private:
    std::vector<jBarrier_Vulkan> Barriers;
};
