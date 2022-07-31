#pragma once

class jCommandBuffer_Vulkan : public jCommandBuffer
{
public:
    virtual void* GetHandle() const override { return CommandBuffer; }
    FORCEINLINE VkCommandBuffer& GetRef() { return CommandBuffer; }
    virtual void* GetFenceHandle() const override { return Fence; }

    virtual void SetFence(void* fence) { Fence = (VkFence)fence; }

    virtual bool Begin() const override
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        // VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT : 커맨드가 한번 실행된다음에 다시 기록됨
        // VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : Single Render Pass 범위에 있는 Secondary Command Buffer.
        // VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : 실행 대기중인 동안에 다시 서밋 될 수 있음.
        beginInfo.flags = 0;					// Optional

        // 이 플래그는 Secondary command buffer를 위해서만 사용하며, Primary command buffer 로 부터 상속받을 상태를 명시함.
        beginInfo.pInheritanceInfo = nullptr;	// Optional

        if (!ensure(vkBeginCommandBuffer(CommandBuffer, &beginInfo) == VK_SUCCESS))
            return false;

        return true;
    }
    virtual bool End() const override
    {
        if (!ensure(vkEndCommandBuffer(CommandBuffer) == VK_SUCCESS))
            return false;

        return true;
    }

    virtual void Reset() const override
    {
        vkResetCommandBuffer(CommandBuffer, 0);
    }

private:
    VkFence Fence = nullptr;
    VkCommandBuffer CommandBuffer = nullptr;
};

class jCommandBufferManager_Vulkan // base 없음
{
public:
    bool CreatePool(uint32 QueueIndex);
    void Release()
    {
        //// Command buffer pool 을 다시 만들기 보다 있는 커맨드 버퍼 풀을 cleanup 하고 재사용 함.
        //vkFreeCommandBuffers(device, CommandBufferManager.GetPool(), static_cast<uint32>(commandBuffers.size()), commandBuffers.data());
    }

    FORCEINLINE const VkCommandPool& GetPool() const { return CommandPool; };

    jCommandBuffer_Vulkan GetOrCreateCommandBuffer();
    void ReturnCommandBuffer(jCommandBuffer_Vulkan commandBuffer);

private:
    VkCommandPool CommandPool;		// 커맨드 버퍼를 저장할 메모리 관리자로 커맨드 버퍼를 생성함.
    std::vector<jCommandBuffer_Vulkan> UsingCommandBuffers;
    std::vector<jCommandBuffer_Vulkan> PendingCommandBuffers;
};

