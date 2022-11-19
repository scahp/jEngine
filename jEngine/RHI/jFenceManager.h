#pragma once

class jFence
{
public:
    virtual ~jFence() {}
    virtual void* GetHandle() const = 0;
};

class jFenceManager
{
public:
    virtual ~jFenceManager() {}
    virtual jFence* GetOrCreateFence() = 0;
    virtual void ReturnFence(jFence* fence) = 0;
    virtual void Release() = 0;
};

#if USE_OPENGL

#elif USE_VULKAN

class jFence_Vulkan : public jFence
{
public:
    virtual ~jFence_Vulkan() {}
    void* GetHandle() const { return Fence; }

    VkFence Fence = nullptr;
};

class jFenceManager_Vulkan : public jFenceManager
{
public:
    virtual jFence* GetOrCreateFence() override;
    virtual void ReturnFence(jFence* fence) override
    {
        UsingFences.erase(fence);
        PendingFences.insert(fence);
    }
    virtual void Release() override;

    robin_hood::unordered_set<jFence*> UsingFences;
    robin_hood::unordered_set<jFence*> PendingFences;
};

#endif