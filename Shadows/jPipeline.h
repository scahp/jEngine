#pragma once
#include "jRHI.h"

class jObject;
class jCamera;
class jLight;
class jGBuffer;

// 고민해볼 점
// 하나의 픽셀을 그려내는 방식을 정의하는 것은 렌더링 파이프라인으로 인해 결정 된다.
// 그러므로 사용하는 쉐이더 + SSBO 와 Uniforms 들을 모두 묶어서 하나의 파이프라인을 구성하도록 하자.
// 디퍼드렌더링의 ShadowPass, RenderPass, ComputePass 에서 어떤 파이프라인을 쓰기로 결정했는지 설정해줄 수 있도록 하자.
// * 현재 파이프라인에는 메시의 타입과 쉐이더의 Vertex Input Stream 의 타입은 고려하지 않고 있다. 매칭되지 않은 Input은 무시된다고 가정하고 있음.
//   하지만 이 부분도 매칭을 시켜줄수있도록 해야할것 같다. 또한 쉐이더와 Mesh의 Vertex Input Stream Type을 서로 확인해 매칭이 될수있는지 확인할 수 있는지 알아보자.
//   혹은 그게 안된다면 Mesh 타입을 조사해서 Shader의 Vertex Input Binding 수를 변경하거나 반대의 상황(Vertex Input Binding 수를 확인하여 Mesh의 Vertex Input Stream 수 조정
//    , 단 가능한 모든 Vertex Input Stream과 Shader의 Vertex Input Binding은 고정)이 가능한가 알아보자.

struct jDeepShadowMapBuffers
{
	static constexpr int32 LinkedlistDepth = 50;
	static constexpr auto LinkedListDepthSize = SM_WIDTH * SM_HEIGHT * LinkedlistDepth;

	~jDeepShadowMapBuffers()
	{
		delete AtomicBuffer;
		delete StartElementBuf;
		delete LinkedListEntryDepthAlphaNext;
		delete LinkedListEntryNeighbors;
	}

	IAtomicCounterBuffer* AtomicBuffer = nullptr;
	IShaderStorageBufferObject* StartElementBuf = nullptr;
	IShaderStorageBufferObject* LinkedListEntryDepthAlphaNext = nullptr;
	IShaderStorageBufferObject* LinkedListEntryNeighbors = nullptr;
};


//////////////////////////////////////////////////////////////////////////
// class IPipeline

// jPipelineData
struct jPipelineData
{
	static const std::list<jObject*> emptyObjectList;
	//static const std::list<const jLight*> emptyLightList;
	jPipelineData()
		: Objects(emptyObjectList)// , Lights(emptyLightList)
	{ }
	jPipelineData(const std::list<jObject*>& objects, const jCamera* camera, const std::list<const jLight*>& lights)
		: Objects(objects), Camera(camera), Lights(lights)
	{ }

	const std::list<jObject*>& Objects;
	const jCamera* Camera = nullptr;
	const std::list<const jLight*> Lights;
};

class IPipeline
{
public:
	IPipeline() = default;
	virtual ~IPipeline() { }

	virtual void Setup() {}
	virtual void Teardown() {}

	virtual void Do(const jPipelineData& pipelineData) = 0;

protected:
	jShader* Shader = nullptr;
	Vector4 ClearColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	ERenderBufferType ClearType = ERenderBufferType::NONE;
	bool EnableDepthTest = true;
	EDepthStencilFunc DepthStencilFunc = EDepthStencilFunc::LEQUAL;
	bool EnableBlend = false;
	EBlendSrc BlendSrc = EBlendSrc::ONE;
	EBlendDest BlendDest = EBlendDest::ONE_MINUS_SRC_ALPHA;
	bool EnableDepthBias = true;
	float DepthSlopeBias = 1.0f;
	float DepthConstantBias = 1.0f;
	std::vector<IBuffer*> Buffers;
};

//////////////////////////////////////////////////////////////////////////
// jRenderPipeline
class jRenderPipeline : public IPipeline
{
public:
	virtual void Do(const jPipelineData& pipelineData) override;
	virtual void Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const;
};


class jDeferredGeometryPipeline : public jRenderPipeline
{
public:
	jDeferredGeometryPipeline() = default;
	jDeferredGeometryPipeline(const jGBuffer* gBuffer)
		: GBuffer(gBuffer)
	{ }

	virtual void Setup() override;
	virtual void Teardown() override {}

	virtual void Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const override;

private:
	const jGBuffer* GBuffer = nullptr;
};

class jDeepShadowMap_ShadowPass_Pipeline : public jRenderPipeline
{
public:
	jDeepShadowMap_ShadowPass_Pipeline() = default;
	jDeepShadowMap_ShadowPass_Pipeline(const jDeepShadowMapBuffers& buffers)
		: DeepShadowMapBuffers(buffers)
	{ }

	virtual void Setup() override;
	virtual void Teardown() override {}
	virtual void Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const override;

private:
	jDeepShadowMapBuffers DeepShadowMapBuffers;
};

class jForwardShadowMap_SSM_Pipeline : public jRenderPipeline
{
public:
	jForwardShadowMap_SSM_Pipeline() = default;

	virtual void Setup() override;
	virtual void Teardown() override;
	virtual void Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const override;

	jShader* ShadowGenShader = nullptr;
	jShader* OmniShadowGenShader = nullptr;
};

class jForward_SSM_Pipeline : public jRenderPipeline
{
public:
	jForward_SSM_Pipeline() = default;

	virtual void Setup() override;
	virtual void Teardown() override;
	virtual void Draw(const std::list<jObject*>& objects, const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights) const override;
};

//////////////////////////////////////////////////////////////////////////
// jComputePipeline
class jComputePipeline : public IPipeline
{
public:
	virtual void Setup() {}
	virtual void Teardown() {}

	virtual void Do(const jPipelineData& pipelineData) override;
	virtual void Dispatch() const;

	uint32 NumGroupsX = 0;
	uint32 NumGroupsY = 0;
	uint32 NumGroupsZ = 0;
	jShader* Shader = nullptr;
};

class jDeepShadowMap_Sort_ComputePipeline : public jComputePipeline
{
public:
	jDeepShadowMap_Sort_ComputePipeline() = default;
	jDeepShadowMap_Sort_ComputePipeline(const jDeepShadowMapBuffers& buffers)
		: DeepShadowMapBuffers(buffers)
	{ }

	virtual void Setup();
	virtual void Teardown();

	jUniformBuffer<int>* ShadowMapWidthUniform = nullptr;
	jUniformBuffer<int>* ShadowMapHeightUniform = nullptr;
	jDeepShadowMapBuffers DeepShadowMapBuffers;
};

class jDeepShadowMap_Link_ComputePipeline : public jComputePipeline
{
public:
	jDeepShadowMap_Link_ComputePipeline() = default;
	jDeepShadowMap_Link_ComputePipeline(const jDeepShadowMapBuffers& buffers)
		: DeepShadowMapBuffers(buffers)
	{ }

	virtual void Setup();
	virtual void Teardown();

	jUniformBuffer<int>* ShadowMapWidthUniform = nullptr;
	jUniformBuffer<int>* ShadowMapHeightUniform = nullptr;
	jDeepShadowMapBuffers DeepShadowMapBuffers;
};

//////////////////////////////////////////////////////////////////////////
// jPipelineSet

enum class EPipelineSetType
{
	None = 0,
	DeepShadowMap,
	Forward,
	Max,
};

class jPipelineSet
{
public:
	virtual ~jPipelineSet() {}
	std::list<IPipeline*> ShadowPrePass;
	std::list<IPipeline*> RenderPass;
	std::list<IPipeline*> PostRenderPass;
	std::list<IPipeline*> UIPass;

	virtual EPipelineSetType GetType() const { return EPipelineSetType::None; }
	virtual void Setup() {}
	virtual void Teardown() {}
};

//////////////////////////////////////////////////////////////////////////
// jDeferredDeepShadowMapPipelineSet
class jDeferredDeepShadowMapPipelineSet : public jPipelineSet
{
public:
	jDeferredDeepShadowMapPipelineSet(const jGBuffer* gBuffer)
		: GBuffer(gBuffer)
	{ }

	virtual EPipelineSetType GetType() const { return EPipelineSetType::DeepShadowMap; }
	virtual void Setup() override;
	virtual void Teardown() override;

	const jGBuffer* GBuffer = nullptr;
	jDeepShadowMapBuffers DeepShadowMapBuffers;
};

//////////////////////////////////////////////////////////////////////////
// jForwardPipelineSet
class jForwardPipelineSet : public jPipelineSet
{
public:
	jForwardPipelineSet() = default;

	virtual EPipelineSetType GetType() const override { return EPipelineSetType::Forward; }

	virtual void Setup() override;
	virtual void Teardown() override;
};
