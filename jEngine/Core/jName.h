#pragma once
#include "jLock.h"

#define jNameStatic(STRING) [&]() {static jName name(STRING); return name; }()

struct jName
{
private:
	static robin_hood::unordered_map<uint32, std::shared_ptr<std::string>> s_NameTable;
	static robin_hood::unordered_map<uint32, std::shared_ptr<std::wstring>> s_WideNameTable;
	static jMutexRWLock Lock;

	// Helper function: Convert wchar_t* to UTF-8 char*
	static std::string WideToUtf8(const wchar_t* wstr, size_t wlen)
	{
		if (!wstr || wlen == 0)
			return std::string();

		// Calculate required buffer size
		int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wlen), nullptr, 0, nullptr, nullptr);
		if (utf8Size <= 0)
			return std::string();

		// Convert to UTF-8
		std::string result(utf8Size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(wlen), &result[0], utf8Size, nullptr, nullptr);
		return result;
	}

	// Helper function: Convert UTF-8 char* to wchar_t*
	static std::wstring Utf8ToWide(const char* str, size_t len)
	{
		if (!str || len == 0)
			return std::wstring();

		// Calculate required buffer size
		int wideSize = MultiByteToWideChar(CP_UTF8, 0, str, static_cast<int>(len), nullptr, 0);
		if (wideSize <= 0)
			return std::wstring();

		// Convert to UTF-16
		std::wstring result(wideSize, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, str, static_cast<int>(len), &result[0], wideSize);
		return result;
	}

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
		NameStringW = nullptr;
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

	FORCEINLINE explicit jName(const wchar_t* pName)
	{
		SetFromWide(pName, wcslen(pName));
	}

	FORCEINLINE explicit jName(const wchar_t* pName, size_t wlen)
	{
		SetFromWide(pName, wlen);
	}

	FORCEINLINE explicit jName(const std::wstring& name)
	{
		SetFromWide(name.c_str(), name.length());
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
			
			// Try again, to avoid entering creation section simultaneously.
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

	FORCEINLINE void SetFromWide(const wchar_t* pName, size_t wlen)
	{
		check(pName);

		// Convert wchar_t* to UTF-8 char*
		std::string utf8String = WideToUtf8(pName, wlen);

		// Use the existing Set function with UTF-8 string
		Set(utf8String.c_str(), utf8String.length());
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

	FORCEINLINE const wchar_t* ToWStr() const
	{
		if (!IsValid())
			return nullptr;

		// Return cached wchar_t* if available
		if (NameStringW)
			return NameStringW;

		// Get UTF-8 string first
		const char* utf8Str = ToStr();
		if (!utf8Str)
			return nullptr;

		// Convert to wchar_t* and cache in s_WideNameTable
		{
			jScopeWriteLock sw(&Lock);

			// Check again after acquiring write lock
			const auto it_find_wide = s_WideNameTable.find(NameHash);
			if (it_find_wide != s_WideNameTable.end())
			{
				NameStringW = it_find_wide->second->c_str();
				return NameStringW;
			}

			// Convert and store
			std::wstring wideString = Utf8ToWide(utf8Str, NameStringLength > 0 ? NameStringLength : strlen(utf8Str));
			const auto it_ret = s_WideNameTable.emplace(NameHash, std::make_shared<std::wstring>(wideString));
			if (it_ret.second)
			{
				NameStringW = it_ret.first->second->c_str();
				return NameStringW;
			}
		}

		return nullptr;
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
	mutable const wchar_t* NameStringW = nullptr;
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

    FORCEINLINE explicit jPriorityName(const wchar_t* pName, uint32 InPriority)
        : jName(pName), Priority(InPriority)
    {}

    FORCEINLINE explicit jPriorityName(const wchar_t* pName, size_t size, uint32 InPriority)
        : jName(pName, size), Priority(InPriority)
    {}

    FORCEINLINE explicit jPriorityName(const std::wstring& name, uint32 InPriority)
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
