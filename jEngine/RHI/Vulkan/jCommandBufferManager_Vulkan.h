#pragma once
#include "../jCommandBufferManager.h"
#include "../jFenceManager.h"
#include "jResourceBarrierBatcher_Vulkan.h"

class jSemaphore_Vulkan;

// Vulkan is using Semaphore for sync between CommandQueues
struct jSyncAcrossCommandQueue_Vulkan : public jSyncAcrossCommandQueue
{
    jSyncAcrossCommandQueue_Vulkan(ECommandBufferType InType, jSemaphore_Vulkan* InWaitSemaphore, uint64 InSemaphoreValue = -1);
    virtual ~jSyncAcrossCommandQueue_Vulkan() {}

    virtual void WaitSyncAcrossCommandQueue(ECommandBufferType InWaitCommandQueueType) override;

    ECommandBufferType Type = ECommandBufferType::MAX;
    jSemaphore_Vulkan* WaitSemaphore = nullptr;
    uint64 SemaphoreValue = 0;      // for timeline semaphore
};

class jCommandBuffer_Vulkan : public jCommandBuffer
{
public:
    jCommandBuffer_Vulkan(ECommandBufferType InType) : jCommandBuffer(InType) {}
    virtual ~jCommandBuffer_Vulkan() {}

    FORCEINLINE const VkCommandBuffer& GetRef() const { return CommandBuffer; }
    FORCEINLINE VkCommandBuffer& GetRef() { return CommandBuffer; }
    virtual void* GetHandle() const override { return CommandBuffer; }
    virtual void* GetFenceHandle() const override { return Fence ? Fence->GetHandle() : nullptr; }
    virtual jFence* GetFence() const override { return Fence; }

    virtual void SetFence(jFence* fence) { Fence = fence; }

    virtual bool Begin() const override;
    virtual bool End() const override;

    virtual void Reset() const override
    {
        vkResetCommandBuffer(CommandBuffer, 0);
    }
    virtual bool IsEnd() const override { return IsClosed; }
    virtual jResourceBarrierBatcher* GetBarrierBatcher() const override { return &BarrierBatcher; }
    virtual void FlushBarrierBatch() const override { BarrierBatcher.Flush(this); }

private:
    jFence* Fence = nullptr;                        // Fence manager 에서 참조하기 때문에 소멸시키지 않음
    VkCommandBuffer CommandBuffer = nullptr;        // CommandBufferManager 에서 CommandBufferPool 을 해제하여 일시에 소멸시키므로 소멸 처리 없음
    mutable bool IsClosed = true;
    mutable jResourceBarrierBatcher_Vulkan BarrierBatcher;
};

class jCommandBufferManager_Vulkan : public jCommandBufferManager
{
public:
    jCommandBufferManager_Vulkan(ECommandBufferType InType) : CommandBufferType(InType) {}
    virtual ~jCommandBufferManager_Vulkan()
    {
        ReleaseInternal();
    }
    virtual bool CreatePool(uint32 QueueIndex);
    virtual void Release() override;

    void ReleaseInternal();

    FORCEINLINE const VkCommandPool& GetPool() const { return CommandPool; };

    virtual jCommandBuffer* GetOrCreateCommandBuffer() override;
    void ReturnCommandBuffer(jCommandBuffer* commandBuffer) override;

    virtual void WaitCommandQueueAcrossSync(const std::shared_ptr<jSyncAcrossCommandQueue>& InSync) override;
    
    void GetWaitSemaphoreAndValueThenClear(std::vector<VkSemaphore>& InOutSemaphore, std::vector<uint64>& InOutSemaphoreValue);
    const std::vector<std::shared_ptr<jSyncAcrossCommandQueue>>& GetWaitSemaphoreAcrossQueues() const { return WaitSemaphoreAcrossQueues; }
    void ClearWaitSemaphoreAcrossQueues() { WaitSemaphoreAcrossQueues.clear(); }

    std::shared_ptr<jSyncAcrossCommandQueue_Vulkan> QueueSubmit(jCommandBuffer_Vulkan* InCommandBuffer, jSemaphore* InWaitSemaphore, jSemaphore* InSignalSemaphore);

    jSemaphore_Vulkan* GetQueueSubmitSemaphore() const { return QueueSubmitSemaphore; }

private:
    ECommandBufferType CommandBufferType = ECommandBufferType::GRAPHICS;
    VkCommandPool CommandPool;		// 커맨드 버퍼를 저장할 메모리 관리자로 커맨드 버퍼를 생성함.

    jMutexLock CommandListLock;
    std::vector<jCommandBuffer_Vulkan*> UsingCommandBuffers;
    std::vector<jCommandBuffer_Vulkan*> AvailableCommandBuffers;

    jSemaphore_Vulkan* QueueSubmitSemaphore = nullptr; // QueueSubmit timeline semaphore, This is equivalent to DX12 fence

    std::vector<std::shared_ptr<jSyncAcrossCommandQueue>> WaitSemaphoreAcrossQueues;
};

