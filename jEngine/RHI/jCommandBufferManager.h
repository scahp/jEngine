#pragma once
#include "jRHIType.h"

class jFence;
class jResourceBarrierBatcher;
struct jQueryPool;

// Make a syncronization between CommandQueues(Graphics, Compute, Copy)
struct jSyncAcrossCommandQueue : std::enable_shared_from_this<jSyncAcrossCommandQueue>
{
    virtual ~jSyncAcrossCommandQueue() {}

    virtual void WaitSyncAcrossCommandQueue(ECommandBufferType InWaitCommandQueueType) {};
};

class jCommandBuffer
{
public:
    const ECommandBufferType Type = ECommandBufferType::GRAPHICS;

    jCommandBuffer(ECommandBufferType InType) : Type(InType) {}
    virtual ~jCommandBuffer() {}

    virtual void* GetHandle() const { return nullptr; }
    virtual void* GetFenceHandle() const { return nullptr; }
    virtual void SetFence(void* fence) {}
    virtual bool Begin() const { return false; }
    virtual bool End() const { return false; }
    virtual void Reset() const {}
    virtual jFence* GetFence() const { return nullptr; }
    virtual bool IsEnd() const { return false; }
    virtual jResourceBarrierBatcher* GetBarrierBatcher() const { return nullptr; }
    virtual void FlushBarrierBatch() const {}
};

class jCommandBufferManager
{
public:
    virtual ~jCommandBufferManager() {}

    virtual void Release() = 0;

    virtual jCommandBuffer* GetOrCreateCommandBuffer() = 0;
    virtual void ReturnCommandBuffer(jCommandBuffer* commandBuffer) = 0;
    
    virtual void WaitCommandQueueAcrossSync(const std::shared_ptr<jSyncAcrossCommandQueue>& InSync) {}
    virtual jQueryPool* GetQueryTimePool() const { return nullptr; }
};
