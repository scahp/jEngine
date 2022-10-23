#pragma once
#include "jBuffer_Vulkan.h"

// Alignment
// 1. 스칼라 타입은 4 bytes align 필요.				Scalars have to be aligned by N(= 4 bytes given 32 bit floats).
// 2. vec2 타입은 8 bytes align 필요.				A vec2 must be aligned by 2N(= 8 bytes)
// 3. vec3 or vec4 타입은 16 bytes align 필요.		A vec3 or vec4 must be aligned by 4N(= 16 bytes)
// 4. 중첩 구조(stuct 같은 것이 멤버인 경우)의 경우 멤버의 기본정렬을 16 bytes 배수로 올림하여 align 되어야 함. 
//													A nested structure must be aligned by the base alignment of its members rounded up to a multiple of 16.
//		예로 C++ 와 Shader 쪽에서 아래와 같이 선언 되어있다고 하자.
//		C++ : struct Foo { Vector2 v; }; struct jUniformBufferObject { Foo f1; Foo f2; };
//		Shader : struct Foo { vec2 v; }; layout(binding = 0) uniform jUniformBufferObject { Foo f1; Foo f2; };
//		Foo는 8 bytes 이지만 C++ 에서 아래와 같이 align을 16으로 맞춰줘야 한다. 이유는 중첩구조(struct 형태의 멤버변수)이기 때문.
//			-> 이렇게 해줘야 함. struct jUniformBufferObject { Foo f1; alignas(16) Foo f2; };
// 5. mat4 타입은 vec4 처럼 16 bytes align 필요.		A mat4 matrix must have the same alignment as a vec4.
// 자세한 설명 : https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/chap14.html#interfaces-resources-layout
// 아래와 같은 경우 맨 처음 등장하는 Vector2 때문에 뒤에나오는 Matrix 가 16 Bytes align 되지 못함.
//struct jUniformBufferObject		|		layout(binding = 0) uniform UniformBufferObject
//{									|		{
//	Vector2 foo;					|			vec2 foo;
//	Matrix Model;					|			mat4 model;
//	Matrix View;					|			mat4 view;
//	Matrix Proj;					|			mat4 proj
//};								|		};
// 이 경우 아래와 같이 alignas(16) 을 써줘서 model 부터 16 bytes align 하면 정상작동 할 수 있음.
//struct jUniformBufferObject		|		layout(binding = 0) uniform UniformBufferObject
//{									|		{
//	Vector2 foo;					|			vec2 foo;
//	alignas(16)Matrix Model;		|			mat4 model;
//	Matrix View;					|			mat4 view;
//	Matrix Proj;					|			mat4 proj
//};								|		};

struct jUniformBufferBlock_Vulkan : public IUniformBufferBlock
{
    using IUniformBufferBlock::IUniformBufferBlock;
    using IUniformBufferBlock::UpdateBufferData;
    virtual ~jUniformBufferBlock_Vulkan()
    {}

    virtual void Init(size_t size) override;
    virtual void UpdateBufferData(const void* InData, size_t InSize) override;

    virtual void ClearBuffer(int32 clearValue) override;

    virtual void* GetBuffer() const override { return Buffer.Buffer; }
    virtual void* GetBufferMemory() const override { return Buffer.BufferMemory; }

    virtual size_t GetBufferSize() const override { return Buffer.AllocatedSize; }
    virtual size_t GetBufferOffset() const override { return Buffer.Offset; }

    jBuffer_Vulkan Buffer;

private:
    jUniformBufferBlock_Vulkan(const jUniformBufferBlock_Vulkan&) = delete;
    jUniformBufferBlock_Vulkan& operator=(const jUniformBufferBlock_Vulkan&) = delete;
};
