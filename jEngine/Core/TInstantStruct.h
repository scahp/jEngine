#pragma once

// jInstantStructInternal
// Recursive declaration of variables that passed into variadic template arguments
template<typename... Args>
struct jInstantStructInternal
{};

template<typename T, typename... Args>
struct jInstantStructInternal<T, Args...> : public jInstantStructInternal<Args...>
{
public:
	static_assert(std::is_trivially_copyable<T>::value, "InstanceStruct members should be 'trivially copyable'");
	static_assert(!std::is_pointer<T>::value, "InstanceStruct members should not be 'pointer'");
	T Data{};
	jInstantStructInternal<T, Args...>(T v, Args... args) : Data(v), jInstantStructInternal<Args...>(args...) {}
};

template<>
struct jInstantStructInternal<> {
public:
};

// jInstantStruct
// The struct that is generated from constructor's arguments
// 생성자로 부터 입력받은 변수로 부터 struct 를 생성하는 것을 목적으로 하는 구조체
template<typename... Args>
struct jInstantStruct : public jInstantStructInternal<Args...>
{
	jInstantStruct(Args... args) : jInstantStructInternal<Args...>(args...) {}
};

// Easy hash generation from InstantStruct, it is slow calling 'Hash Generation Function' for each variables Indivisually.
#define GETHASH_FROM_INSTANT_STRUCT(...) \
[&]() -> uint64 { auto InstanceStruct = jInstantStruct(__VA_ARGS__); return CityHash64((const char*)&InstanceStruct, sizeof(InstanceStruct)); }()
