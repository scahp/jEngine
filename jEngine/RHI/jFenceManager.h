#pragma once

class jFence
{
public:
    virtual ~jFence() {}
    virtual void* GetHandle() const = 0;
    virtual void Release() = 0;
    virtual void WaitForFence(uint64 InTimeoutNanoSec = UINT64_MAX) = 0;
    virtual bool SetFenceValue(uint64 InFenceValue) { return false; }
    virtual bool IsValid() const { return false; }
};

class jFenceManager
{
public:
    virtual ~jFenceManager() {}
    virtual jFence* GetOrCreateFence() = 0;
    virtual void ReturnFence(jFence* fence) = 0;
    virtual void Release() = 0;
};
