﻿#pragma once
#include "jLock.h"

template <typename T, typename LOCK_TYPE = jEmtpyRWLock>
class TResourcePool
{
public:
    template <typename TInitializer, typename T1 = T>
    T* GetOrCreate(const TInitializer& initializer)
    {
        const size_t hash = initializer.GetHash();
        {
            jScopeReadLock sr(&Lock);
            auto it_find = Pool.find(hash);
            if (Pool.end() != it_find)
            {
                return it_find->second;
            }
        }

        {
            jScopeWriteLock sw(&Lock);

            // Try again, to avoid entering creation section simultaneously.
            auto it_find = Pool.find(hash);
            if (Pool.end() != it_find)
            {
                return it_find->second;
            }

            auto* newResource = new T1(initializer);
            newResource->Initialize();
            Pool[hash] = newResource;
            return newResource;
        }
    }

    template <typename TInitializer, typename T1 = T>
    T* GetOrCreateMove(TInitializer&& initializer)
    {
        const size_t hash = initializer.GetHash();
        {
            jScopeReadLock sr(&Lock);
            auto it_find = Pool.find(hash);
            if (Pool.end() != it_find)
            {
                return it_find->second;
            }
        }

        {
            jScopeWriteLock sw(&Lock);

            // Try again, to avoid entering creation section simultaneously.
            auto it_find = Pool.find(hash);
            if (Pool.end() != it_find)
            {
                return it_find->second;
            }

            auto* newResource = new T1(std::move(initializer));
            newResource->Initialize();
            Pool[hash] = newResource;
            return newResource;
        }
    }

    template <typename TInitializer, typename T1 = T>
    void Add(TInitializer&& initializer, T1* InResource)
    {
        jScopeReadLock sr(&Lock);
        const size_t hash = initializer.GetHash();
        check(hash);

        if (ensure(InResource))
        {
            Pool[hash] = InResource;
        }
    }

    void GetAllResource(std::vector<T*>& Out)
    {
        Out.reserve(Pool.size());
        for(auto it : Pool)
        {
            Out.push_back(it.second);
        }
    }

    template <typename TInitializer>
    T* Release(const TInitializer& initializer)
    {
        return Release(initializer.GetHash());
    }

    T* Release(size_t InHash)
    {
        jScopeReadLock sr(&Lock);
        auto it_find = Pool.find(InHash);
        if (Pool.end() != it_find)
        {
            T* ToRelease = it_find->second;
            Pool.erase(it_find);
            return ToRelease;
        }
        return nullptr;
    }

    void ReleaseAll()
    {
        jScopeWriteLock sw(&Lock);
        for (auto& iter : Pool)
        {
            static_assert(sizeof(T) > 0, "Cannot delete pointer of incomplete type");
            delete iter.second;
        }
        Pool.clear();
    }

    robin_hood::unordered_map<size_t, T*> Pool;
    LOCK_TYPE Lock;
};
