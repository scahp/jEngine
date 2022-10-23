#pragma once
#include "jLock.h"

#define jNameStatic(STRING) [&]() {static jName name(STRING); return name; }()

struct jName
{
private:
	static robin_hood::unordered_map<uint32, std::shared_ptr<std::string>> s_NameTable;
	static jMutexRWLock Lock;

public:
	const static jName Invalid;

	FORCEINLINE static uint32 GenerateNameHash(const char* pName, size_t size)
	{
		return CityHash32(pName, size);
	}

	jName() = default;

	FORCEINLINE explicit jName(uint32 InNameHash)
	{
		NameHash = InNameHash;
		NameString = nullptr;
		NameStringLength = 0;
	}

	FORCEINLINE explicit jName(const char* pName)
	{
		Set(pName, strlen(pName));
	}

	FORCEINLINE explicit jName(const char* pName, size_t size)
	{
		Set(pName, size);
	}

	FORCEINLINE explicit jName(const std::string& name)
	{
		Set(name.c_str(), name.length());
	}

	FORCEINLINE jName(const jName& name)
	{
		*this = name;
	}

	FORCEINLINE void Set(const char* pName, size_t size)
	{
		check(pName);
		const uint32 NewNameHash = GenerateNameHash(pName, size);

		auto find_func = [&]()
		{
            const auto find_it = s_NameTable.find(NewNameHash);
            if (s_NameTable.end() != find_it)
            {
                NameHash = NewNameHash;
                NameString = find_it->second->c_str();
                NameStringLength = find_it->second->size();
                return true;
            }
			return false;
		};

		{
			jScopeReadLock sr(&Lock);
			if (find_func())
				return;
		}

		{
			jScopeWriteLock sw(&Lock);
			
			// Try again, to avoid entering creation section simultanteously.
            if (find_func())
                return;

			const auto it_ret = s_NameTable.emplace(NewNameHash, CreateNewName_Internal(pName, NewNameHash));
			if (it_ret.second)
			{
				NameHash = NewNameHash;
				NameString = it_ret.first->second->c_str();
				NameStringLength = it_ret.first->second->size();
				return;
			}
		}

		check(0);
	}

	FORCEINLINE operator uint32() const
	{
		check(NameHash != -1);
		return NameHash;
	}

	FORCEINLINE jName& operator = (const jName& In)
	{
		NameHash = In.NameHash;
        NameString = In.NameString;
        NameStringLength = In.NameStringLength;
		return *this;
	}

	FORCEINLINE bool operator == (const jName& rhs) const
	{
		return GetNameHash() == rhs.GetNameHash();
	}

	FORCEINLINE bool IsValid() const { return NameHash != -1; }
	FORCEINLINE const char* ToStr() const 
	{ 
		if (!IsValid())
			return nullptr;

		if (NameString)
			return NameString;

		{
			jScopeReadLock s(&Lock);
			const auto it_find = s_NameTable.find(NameHash);
			if (it_find == s_NameTable.end())
				return nullptr;

			NameString = it_find->second->c_str();
			NameStringLength = it_find->second->size();

			return it_find->second->c_str();
		}
	}
	FORCEINLINE const size_t GetStringLength() const
	{
		if (!IsValid())
			return 0;

		if (!NameStringLength)
			return NameStringLength;

		{
			jScopeReadLock s(&Lock);
			const auto it_find = s_NameTable.find(NameHash);
			if (it_find == s_NameTable.end())
				return 0;

			NameString = it_find->second->c_str();
			NameStringLength = it_find->second->size();

			return NameStringLength;
		}
	}
	FORCEINLINE uint32 GetNameHash() const { return NameHash; }

private:
	FORCEINLINE static std::shared_ptr<std::string> CreateNewName_Internal(const char* pName, uint32 NameHash)
	{
		check(pName);

		return std::make_shared<std::string>(pName);
	}

	uint32 NameHash = -1;
	mutable const char* NameString = nullptr;
	mutable size_t NameStringLength = 0;
};

struct jNameHashFunc
{
    std::size_t operator()(const jName& name) const
    {
        return static_cast<size_t>(name.GetNameHash());
    }
};

struct jPriorityName : public jName
{
	jPriorityName() = default;

    FORCEINLINE explicit jPriorityName(uint32 InNameHash, uint32 InPriority)
		: jName(InNameHash), Priority(InPriority)
    {}

    FORCEINLINE explicit jPriorityName(const char* pName, uint32 InPriority)
        : jName(pName), Priority(InPriority)
    {}

    FORCEINLINE explicit jPriorityName(const char* pName, size_t size, uint32 InPriority)
        : jName(pName, size), Priority(InPriority)
    {}

    FORCEINLINE explicit jPriorityName(const std::string& name, uint32 InPriority)
        : jName(name), Priority(InPriority)
    {}

    FORCEINLINE explicit jPriorityName(const jName& name, uint32 InPriority)
        : jName(name), Priority(InPriority)
    {}

	uint32 Priority = 0;
};

struct jPriorityNameHashFunc
{
    std::size_t operator()(const jPriorityName& name) const
    {
        return name.GetNameHash();
    }
};

struct jPriorityNameComapreFunc
{
    bool operator()(const jPriorityName& lhs, const jPriorityName& rhs) const
    {
        return lhs.Priority < rhs.Priority;
    }
};
