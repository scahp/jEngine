﻿#pragma once
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
    VkFence Fence = nullptr;                        // Fence manager 에서 참조하기 때문에 소멸시키지 않음
    VkCommandBuffer CommandBuffer = nullptr;        // CommandBufferManager 에서 CommandBufferPool 을 해제하여 일시에 소멸시키므로 소멸 처리 없음
};

class jCommandBufferManager_Vulkan : public jCommandBufferManager
{
public:
    virtual ~jCommandBufferManager_Vulkan()
    {
        jCommandBufferManager_Vulkan::Release();
    }
    virtual bool CreatePool(uint32 QueueIndex) override;
    virtual void Release() override;

    FORCEINLINE const VkCommandPool& GetPool() const { return CommandPool; };

    virtual jCommandBuffer* GetOrCreateCommandBuffer() override;
    void ReturnCommandBuffer(jCommandBuffer* commandBuffer) override;

private:
    VkCommandPool CommandPool;		// 커맨드 버퍼를 저장할 메모리 관리자로 커맨드 버퍼를 생성함.
    std::vector<jCommandBuffer_Vulkan*> UsingCommandBuffers;
    std::vector<jCommandBuffer_Vulkan*> PendingCommandBuffers;
};
