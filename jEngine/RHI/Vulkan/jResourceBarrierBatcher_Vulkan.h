#pragma once
#include "RHI/jResourceBarrierBatcher.h"

struct jBarrier_Vulkan
{
    FORCEINLINE jBarrier_Vulkan(VkPipelineStageFlags InSrcStage, VkPipelineStageFlags InDstStage)
        : SrcStage(InSrcStage), DstStage(InDstStage) {}
    FORCEINLINE jBarrier_Vulkan(VkPipelineStageFlags InSrcStage, VkPipelineStageFlags InDstStage, const VkBufferMemoryBarrier& InBarrier)
        : SrcStage(InSrcStage), DstStage(InDstStage), BufferBarriers{InBarrier} {}
    FORCEINLINE jBarrier_Vulkan(VkPipelineStageFlags InSrcStage, VkPipelineStageFlags InDstStage, const VkImageMemoryBarrier& InBarrier)
        : SrcStage(InSrcStage), DstStage(InDstStage), ImageBarriers{InBarrier} {}
    FORCEINLINE jBarrier_Vulkan(VkPipelineStageFlags InSrcStage, VkPipelineStageFlags InDstStage, const VkMemoryBarrier& InBarrier)
        : SrcStage(InSrcStage), DstStage(InDstStage), MemroyBarriers{ InBarrier } {}

    FORCEINLINE bool CanMerge(VkPipelineStageFlags InSrcStage, VkPipelineStageFlags InDstStage) const
    {
        return SrcStage == InSrcStage && DstStage == InDstStage;
    }

    FORCEINLINE void AddBarrier(const VkBufferMemoryBarrier& InBarrier) { BufferBarriers.push_back(InBarrier); }
    FORCEINLINE void AddBarrier(const VkImageMemoryBarrier& InBarrier) { ImageBarriers.push_back(InBarrier); }
    FORCEINLINE void AddBarrier(const VkMemoryBarrier& InBarrier) { MemroyBarriers.push_back(InBarrier); }

    VkPipelineStageFlags SrcStage = 0;
    VkPipelineStageFlags DstStage = 0;

    std::vector<VkBufferMemoryBarrier> BufferBarriers;
    std::vector<VkImageMemoryBarrier> ImageBarriers;
	std::vector<VkMemoryBarrier> MemroyBarriers;
};

class jResourceBarrierBatcher_Vulkan : public jResourceBarrierBatcher
{
public:
    virtual ~jResourceBarrierBatcher_Vulkan() {}

    // Vulkan UAVBarrier doesn't need to resource, because it works with pipeline stage.
    // - https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples-(Legacy-synchronization-APIs)
    virtual void AddUAV(jBuffer* InBuffer) override;
    virtual void AddUAV(jTexture* InTexture) override;

    virtual void AddTransition(jBuffer* InBuffer, EResourceLayout InNewLayout) override;
    virtual void AddTransition(jTexture* InTexture, EResourceLayout InNewLayout) override;

    virtual void Flush(const jCommandBuffer* InCommandBuffer) override;

    template <typename T>
    void AddBarrier(VkPipelineStageFlags InSrcStage, VkPipelineStageFlags InDstStage, const T& InBarrier)
    {
		// Function to alloc NewBarriers
		auto AllocateNewBarriers = [&](VkPipelineStageFlags InSrcStage, VkPipelineStageFlags InDstStage, const T & InNewBarrier)
		{
			BarriersList.push_back(jBarrier_Vulkan(InSrcStage, InDstStage, InNewBarrier));
			CurrentBarriers = &(*BarriersList.rbegin());
		};

		if (BarriersList.empty())
		{
			AllocateNewBarriers(InSrcStage, InDstStage, InBarrier);
			return;
		}
		check(CurrentBarriers);

		// Check the new barrier could be merged with CurrentBarries, if not split the barrieres array.
		if (CurrentBarriers->CanMerge(InSrcStage, InDstStage))
		{
			CurrentBarriers->AddBarrier(InBarrier);
		}
		else
		{
			AllocateNewBarriers(InSrcStage, InDstStage, InBarrier);
		}
    }

    void ClearBarriers();

private:    
    jBarrier_Vulkan* CurrentBarriers = nullptr;
    std::list<jBarrier_Vulkan> BarriersList;
};
