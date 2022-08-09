#pragma once
#include "../jCommandBufferManager.h"

class jCommandBuffer_Vulkan : public jCommandBuffer
{
public:
    FORCEINLINE VkCommandBuffer& GetRef() { return CommandBuffer; }
    virtual void* GetHandle() const override { return CommandBuffer; }
    virtual void* GetFenceHandle() const override { return Fence; }

    virtual void SetFence(void* fence) { Fence = (VkFence)fence; }

    virtual bool Begin() const override;
    virtual bool End() const override;

    virtual void Reset() const override
    {
        vkResetCommandBuffer(CommandBuffer, 0);
    }

private:
    VkFence Fence = nullptr;
    VkCommandBuffer CommandBuffer = nullptr;
};

class jCommandBufferManager_Vulkan : public jCommandBufferManager
{
public:
    virtual bool CreatePool(uint32 QueueIndex) override;
    virtual void Release() override
    {
        //// Command buffer pool 을 다시 만들기 보다 있는 커맨드 버퍼 풀을 cleanup 하고 재사용 함.
        //vkFreeCommandBuffers(device, CommandBufferManager.GetPool(), static_cast<uint32>(commandBuffers.size()), commandBuffers.data());
    }

    FORCEINLINE const VkCommandPool& GetPool() const { return CommandPool; };

    virtual jCommandBuffer* GetOrCreateCommandBuffer() override;
    void ReturnCommandBuffer(jCommandBuffer* commandBuffer) override;

private:
    VkCommandPool CommandPool;		// 커맨드 버퍼를 저장할 메모리 관리자로 커맨드 버퍼를 생성함.
    std::vector<jCommandBuffer_Vulkan*> UsingCommandBuffers;
    std::vector<jCommandBuffer_Vulkan*> PendingCommandBuffers;
};

