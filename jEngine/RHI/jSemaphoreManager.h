#pragma once

class jSemaphore
{
public:
    virtual ~jSemaphore() {}
    virtual void* GetHandle() const = 0;
};

class jSemaphoreManager
{
public:
    virtual ~jSemaphoreManager() {}
    virtual jSemaphore* GetOrCreateSemaphore() = 0;
    virtual void ReturnSemaphore(jSemaphore* fence) = 0;
};

class jSemaphore_Vulkan : public jSemaphore
{
public:
    virtual ~jSemaphore_Vulkan() {}
    void* GetHandle() const { return Semaphore; }

    VkSemaphore Semaphore = nullptr;
};


class jSemaphoreManager_Vulkan : public jSemaphoreManager
{
public:
    virtual ~jSemaphoreManager_Vulkan();
    virtual jSemaphore* GetOrCreateSemaphore() override;
    virtual void ReturnSemaphore(jSemaphore* fence) override
    {
        UsingSemaphore.erase(fence);
        PendingSemaphore.insert(fence);
    }
    void Release();

    robin_hood::unordered_set<jSemaphore*> UsingSemaphore;
    robin_hood::unordered_set<jSemaphore*> PendingSemaphore;
};

//#endif