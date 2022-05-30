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
			return;
		}

		const auto it_ret = s_NameTable.emplace(NewNameHash, CreateNewName_Internal(pName, NewNameHash));
		if (it_ret.second)
		{
			NameHash = NewNameHash;
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
		return *this;
	}

	FORCEINLINE bool IsValid() const { return NameHash != -1; }
	FORCEINLINE const char* ToStr() const 
	{ 
		if (!IsValid())
			return nullptr;

		const auto it_find = s_NameTable.find(NameHash);
		if (it_find == s_NameTable.end())
			return nullptr;

		return it_find->second->c_str();
	}
	FORCEINLINE uint32 GetNameHash() const { return NameHash; }

private:
	FORCEINLINE static std::shared_ptr<std::string> CreateNewName_Internal(const char* pName, uint32 NameHash)
	{
		check(pName);

		return std::shared_ptr<std::string>(new std::string(pName));
	}

	uint32 NameHash = -1;
};
