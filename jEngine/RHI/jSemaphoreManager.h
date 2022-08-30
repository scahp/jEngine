#pragma once

class jSemaphoreManager
{
public:
    virtual ~jSemaphoreManager() {}
    virtual VkSemaphore GetOrCreateSemaphore() = 0;
    virtual void ReturnSemaphore(VkSemaphore fence) = 0;
};

#if USE_OPENGL

#elif USE_VULKAN

class jSemaphoreManager_Vulkan : public jSemaphoreManager
{
public:
    virtual ~jSemaphoreManager_Vulkan();
    virtual VkSemaphore GetOrCreateSemaphore() override;
    virtual void ReturnSemaphore(VkSemaphore fence) override
    {
        UsingSemaphore.erase(fence);
        PendingSemaphore.insert(fence);
    }
    void ReleaseInternal();

    std::unordered_set<VkSemaphore> UsingSemaphore;
    std::unordered_set<VkSemaphore> PendingSemaphore;
};

#endif