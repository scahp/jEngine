﻿#pragma once

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
};

class jCommandBufferManager
{
public:
    virtual ~jCommandBufferManager() {}

    virtual bool CreatePool(uint32 QueueIndex) = 0;
    virtual void Release() = 0;

    virtual jCommandBuffer* GetOrCreateCommandBuffer() = 0;
    virtual void ReturnCommandBuffer(jCommandBuffer* commandBuffer) = 0;
};