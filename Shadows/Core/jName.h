#pragma once

struct jName
{
private:
	static std::unordered_map<uint32, std::shared_ptr<std::string>> s_NameTable;

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

		const auto find_it = s_NameTable.find(NewNameHash);
		if (s_NameTable.end() != find_it)
		{
			NameHash = NewNameHash;
            NameString = find_it->second->c_str();
            NameStringLength = find_it->second->size();
			return;
		}

		const auto it_ret = s_NameTable.emplace(NewNameHash, CreateNewName_Internal(pName, NewNameHash));
		if (it_ret.second)
		{
			NameHash = NewNameHash;
			NameString = it_ret.first->second->c_str();
			NameStringLength = it_ret.first->second->size();
			return;
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

		const auto it_find = s_NameTable.find(NameHash);
		if (it_find == s_NameTable.end())
			return nullptr;

        NameString = it_find->second->c_str();
        NameStringLength = it_find->second->size();

		return it_find->second->c_str();
	}
	FORCEINLINE const size_t GetStringLength() const
	{
		if (!IsValid())
			return 0;

		if (!NameStringLength)
			return NameStringLength;

		const auto it_find = s_NameTable.find(NameHash);
		if (it_find == s_NameTable.end())
			return 0;

		NameString = it_find->second->c_str();
		NameStringLength = it_find->second->size();

		return NameStringLength;
	}
	FORCEINLINE uint32 GetNameHash() const { return NameHash; }

private:
	FORCEINLINE static std::shared_ptr<std::string> CreateNewName_Internal(const char* pName, uint32 NameHash)
	{
		check(pName);

		return std::shared_ptr<std::string>(new std::string(pName));
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