#pragma once

enum class ESemaphoreType : uint8
{
    BINARY = 0,
    TIMELINE,
    MAX
};

class jSemaphore
{
public:
    virtual ~jSemaphore() {}
    virtual void* GetHandle() const = 0;
    virtual uint64 GetValue() const = 0;
    virtual uint64 IncrementValue() = 0;
    virtual ESemaphoreType GetType() const = 0;
};

class jSemaphoreManager
{
public:
    virtual ~jSemaphoreManager() {}
    virtual jSemaphore* GetOrCreateSemaphore(ESemaphoreType InType) = 0;
    virtual void ReturnSemaphore(jSemaphore* fence) = 0;
};

class jSemaphore_Vulkan : public jSemaphore
{
public:
    jSemaphore_Vulkan(ESemaphoreType InType) : Type(InType) {}

    virtual ~jSemaphore_Vulkan() {}
    virtual void* GetHandle() const override { return Semaphore; }
    virtual uint64 GetValue() const override { return SignalValue; }
    virtual uint64 IncrementValue() override { return (Type == ESemaphoreType::BINARY) ? 1 : ++SignalValue; }
    virtual ESemaphoreType GetType() const override { return Type; }

    ESemaphoreType Type = ESemaphoreType::BINARY;
    VkSemaphore Semaphore = nullptr;
    uint64 SignalValue = 0;
};


class jSemaphoreManager_Vulkan : public jSemaphoreManager
{
public:
    virtual ~jSemaphoreManager_Vulkan();
    virtual jSemaphore* GetOrCreateSemaphore(ESemaphoreType InType) override;
    virtual void ReturnSemaphore(jSemaphore* fence) override
    {
        check(fence);
        UsingSemaphore[(int32)fence->GetType()].erase(fence);
        PendingSemaphore[(int32)fence->GetType()].insert(fence);
    }
    void Release();

    robin_hood::unordered_set<jSemaphore*> UsingSemaphore[(int32)ESemaphoreType::MAX];
    robin_hood::unordered_set<jSemaphore*> PendingSemaphore[(int32)ESemaphoreType::MAX];
};

//#endif