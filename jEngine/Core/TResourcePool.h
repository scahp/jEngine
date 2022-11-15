#pragma once
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

            // Try again, to avoid entering creation section simultanteously.
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

            // Try again, to avoid entering creation section simultanteously.
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

    void Release()
    {
        jScopeWriteLock sw(&Lock);
        for (auto& iter : Pool)
        {
            delete iter.second;
        }
        Pool.clear();
    }

    robin_hood::unordered_map<size_t, T*> Pool;
    LOCK_TYPE Lock;
};
