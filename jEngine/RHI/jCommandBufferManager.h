#pragma once

class jFence;

class jCommandBuffer
{
public:
    virtual ~jCommandBuffer() {}

    virtual void* GetHandle() const { return nullptr; }
    virtual void* GetFenceHandle() const { return nullptr; }
    virtual void SetFence(void* fence) {}
    virtual bool Begin() const { return false; }
    virtual bool End() const { return false; }
    virtual void Reset() const {}
    virtual jFence* GetFence() const { return nullptr; }
};

class jCommandBufferManager
{
public:
    virtual ~jCommandBufferManager() {}

    virtual void Release() = 0;

    virtual jCommandBuffer* GetOrCreateCommandBuffer() = 0;
    virtual void ReturnCommandBuffer(jCommandBuffer* commandBuffer) = 0;
};
