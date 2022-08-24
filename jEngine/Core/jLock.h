#pragma once

class jLock
{
public:
    virtual ~jLock() {}
    virtual void Lock() {}
    virtual void Unlock() {}
};

using jEmptyLock = jLock;

class jMutexLock : public jLock
{
    void Lock()
    {
        lock.lock();
    }
    void Unlock()
    {
        lock.unlock();
    }

    std::mutex lock;
};

class jRWLock
{
public:
    virtual void LockRead() {}
    virtual void UnlockRead() {}
    virtual void LockWrite() {}
    virtual void UnlockWrite() {}
};

using jEmtpyRWLock = jRWLock;

// RW lock 으로 교체 예정
class jMutexRWLock : public jRWLock
{
public:
    virtual ~jMutexRWLock() {}
    virtual void LockRead() override
    {
        lock.lock_shared();
    }
    virtual void UnlockRead() override
    {
        lock.unlock_shared();
    }
    virtual void LockWrite() override
    {
        lock.lock();
    }
    virtual void UnlockWrite() override
    {
        lock.unlock();
    }

    std::shared_mutex lock;
};

class jScopedLock
{
public:
    jScopedLock(jLock* InLock)
        : ScopedLock(InLock)
    {
        if (ScopedLock)
            ScopedLock->Lock();
    }
    ~jScopedLock()
    {
        if (ScopedLock)
            ScopedLock->Unlock();
    }
    jLock* ScopedLock;
};

class jScopeReadLock
{
public:
    jScopeReadLock(jRWLock* InLock)
        : ScopedLock(InLock)
    {
        if (ScopedLock)
            ScopedLock->LockRead();
    }
    ~jScopeReadLock()
    {
        if (ScopedLock)
            ScopedLock->UnlockRead();
    }
    jRWLock* ScopedLock;
};

class jScopeWriteLock
{
public:
    jScopeWriteLock(jRWLock* InLock)
        : ScopedLock(InLock)
    {
        if (ScopedLock)
            ScopedLock->LockWrite();
    }
    ~jScopeWriteLock()
    {
        if (ScopedLock)
            ScopedLock->UnlockWrite();
    }
    jRWLock* ScopedLock;
};
