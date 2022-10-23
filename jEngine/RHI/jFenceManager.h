#pragma once

class jFenceManager
{
public:
    virtual ~jFenceManager() {}
    virtual VkFence GetOrCreateFence() = 0;
    virtual void ReturnFence(VkFence fence) = 0;
    virtual void Release() = 0;
};

#if USE_OPENGL

#elif USE_VULKAN

class jFenceManager_Vulkan : public jFenceManager
{
public:
    virtual VkFence GetOrCreateFence() override;
    virtual void ReturnFence(VkFence fence) override
    {
        UsingFences.erase(fence);
        PendingFences.insert(fence);
    }
    virtual void Release() override;

    robin_hood::unordered_set<VkFence> UsingFences;
    robin_hood::unordered_set<VkFence> PendingFences;
};

#endif