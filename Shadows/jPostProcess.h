#pragma once

struct jRenderTarget;
struct jShader;
class jCamera;
class jObject;
class jLight;
struct IShaderStorageBufferObject;
class jFullscreenQuadPrimitive;
class jGBuffer;

struct jPostProcessInOutput : public std::enable_shared_from_this< jPostProcessInOutput>
{
	jRenderTarget* RenderTaret = nullptr;
};

//////////////////////////////////////////////////////////////////////////
class IPostprocess
{
public:
	IPostprocess() = default;
	IPostprocess(const std::string& name, std::shared_ptr<jRenderTarget> renderTarget)
		: Name(name), RenderTarget(renderTarget)
	{ }
	virtual ~IPostprocess() {}
	std::string Name;
	std::shared_ptr<jRenderTarget> RenderTarget;
	std::weak_ptr<jPostProcessInOutput> PostProcessInput;
	std::shared_ptr<jPostProcessInOutput> PostProcessOutput;

	virtual void Setup();
	virtual void TearDown() { }

	virtual bool Process(const jCamera* camera) const;

	virtual std::weak_ptr<jPostProcessInOutput> GetPostProcessOutput() const;
	virtual void SetPostProcessInput(const std::weak_ptr<jPostProcessInOutput>& input);
	virtual void SetPostProcessOutput(const std::shared_ptr<jPostProcessInOutput>& output);

protected:
	virtual bool Do(const jCamera* camera) const = 0;

	jFullscreenQuadPrimitive* GetFullscreenQuad() const;
	virtual void Draw(const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights) const;
};

//////////////////////////////////////////////////////////////////////////
class jPostprocessChain
{
public:
	jPostprocessChain() = default;

	void AddNewPostprocess(IPostprocess* postprocess);
	void ReleaseAllPostprocesses();

	bool Process(const jCamera* camera) const;

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
		: IPostprocess(name, renderTarget), SSBOs(ssbos), GBuffer(gBuffer)
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

	virtual void Setup() override;

	virtual bool Do(const jCamera* camera) const override;

private:
	const jShader* Shader = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// Blur
class jPostProcess_Blur : public IPostprocess
{
public:
	jPostProcess_Blur() = default;
	jPostProcess_Blur(const std::string& name, std::shared_ptr<jRenderTarget> renderTarget, bool isVertical)
		: IPostprocess(name, renderTarget), IsVertical(isVertical)
	{ }

	virtual bool Do(const jCamera* camera) const override;

	float MaxDist = FLT_MAX;
	bool IsVertical = false;
	bool OmniDirectional = false;
};
