#pragma once

class jLock
{
public:
    void Lock() {}
    void Unlock() {}
};

using jEmptyLock = jLock;

class jMutexLock
{
public:
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
    void LockRead() {}
    void UnlockRead() {}
    void LockWrite() {}
    void UnlockWrite() {}
};

using jEmtpyRWLock = jRWLock;

// RW lock 으로 교체 예정
class jMutexRWLock
{
public:
    void LockRead()
    {
        lock.lock_shared();
    }
    void UnlockRead()
    {
        lock.unlock_shared();
    }
    void LockWrite()
    {
        lock.lock();
    }
    void UnlockWrite()
    {
        lock.unlock();
    }

    std::shared_mutex lock;
};

template <typename T>
class jScopedLock
{
public:
    jScopedLock(T* InLock)
        : ScopedLock(InLock)
    {
        ScopedLock->Lock();
    }
    ~jScopedLock()
    {
        ScopedLock->Unlock();
    }
    T* ScopedLock;
};

template <typename T>
class jScopeReadLock
{
public:
    jScopeReadLock(T* InLock)
        : ScopedLock(InLock)
    {
        ScopedLock->LockRead();
    }
    ~jScopeReadLock()
    {
        ScopedLock->UnlockRead();
    }
    T* ScopedLock;
};

template <typename T>
class jScopeWriteLock
{
public:
    jScopeWriteLock(T* InLock)
        : ScopedLock(InLock)
    {
        ScopedLock->LockWrite();
    }
    ~jScopeWriteLock()
    {
        ScopedLock->UnlockWrite();
    }
    T* ScopedLock;
};
