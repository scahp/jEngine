#pragma once

struct jRenderTarget;
struct jShader;
class jCamera;
class jObject;
class jLight;
struct IShaderStorageBufferObject;
class jFullscreenQuadPrimitive;
class jGBuffer;

struct jPostProcessInOutput
{
	jRenderTarget* RenderTaret = nullptr;
};

//////////////////////////////////////////////////////////////////////////
class IPostprocess
{
public:
	IPostprocess() = default;
	IPostprocess(const std::string& name, std::shared_ptr<jRenderTarget> renderTarget, const jShader* shader)
		: Name(name), RenderTarget(renderTarget), Shader(shader)
	{ }
	virtual ~IPostprocess() {}
	std::string Name;
	std::shared_ptr<jRenderTarget> RenderTarget;
	const jShader* Shader = nullptr;
	std::weak_ptr<jPostProcessInOutput> PostProcessInput;
	std::shared_ptr<jPostProcessInOutput> PostProcessOutput;

	virtual void Setup();
	virtual void TearDown() { }

	virtual bool Process(const jCamera* camera) const;

	virtual std::weak_ptr<jPostProcessInOutput> GetPostProcessOutput() const;
	virtual void SetPostProcessInput(std::weak_ptr<jPostProcessInOutput> input);

protected:
	virtual bool Do(const jCamera* camera) const = 0;

	jFullscreenQuadPrimitive* GetFullscreenQuad() const;
	virtual void Draw(const jCamera* camera, const jShader* shader, const jLight* directionalLight) const;
};

//////////////////////////////////////////////////////////////////////////
class jPostprocessChain
{
public:
	jPostprocessChain() = default;
	~jPostprocessChain();

	void AddNewPostprocess(IPostprocess* postprocess);
	void ReleaseAllPostprocesses();

	bool Process(const jCamera* camera);

private:
	std::list<IPostprocess*> PostProcesses;
};

//////////////////////////////////////////////////////////////////////////
// DeepShadowMap postprocess
class jPostProcess_DeepShadowMap : public IPostprocess
{
public:
	struct jSSBO_DeepShadowMap
	{
		IShaderStorageBufferObject* StartElementBuf = nullptr;
		IShaderStorageBufferObject* LinkedListEntryDepthAlphaNext = nullptr;
		IShaderStorageBufferObject* LinkedListEntryNeighbors = nullptr;

		void Bind(const jShader* shader) const;
	};

	jPostProcess_DeepShadowMap() = default;
	jPostProcess_DeepShadowMap(const std::string& name, std::shared_ptr<jRenderTarget> renderTarget, const jSSBO_DeepShadowMap& ssbos, const jGBuffer* gBuffer)
		: IPostprocess(name, renderTarget, nullptr), SSBOs(ssbos), GBuffer(gBuffer)
	{ }

	virtual bool Do(const jCamera* camera) const override;

private:
	jSSBO_DeepShadowMap SSBOs;
	const jGBuffer* GBuffer = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// DeepShadowMap anti aliasing postprocess
class jPostProcess_AA_DeepShadowAddition : public IPostprocess
{
public:
	using IPostprocess::IPostprocess;

	virtual void Setup() {}
	virtual void TearDown() {}

	virtual bool Do(const jCamera* camera) const override;
};

