#pragma once

#include "jRHI.h"
#include "jRHIType.h"
#include "jShader.h"

struct jVertexStream_OpenGL
{
	std::string Name;
	unsigned int BufferID = 0;
	unsigned int Count;
	EBufferType BufferType = EBufferType::STATIC;
	EBufferElementType ElementType = EBufferElementType::BYTE;
	bool Normalized = false;
	int Stride = 0;
	size_t Offset = 0;
};

struct jVertexBuffer_OpenGL : public jVertexBuffer
{
	uint32 VAO = 0;
	std::vector<jVertexStream_OpenGL> Streams;

	virtual void Bind(const jShader* shader) const override;
};

struct jIndexBuffer_OpenGL : public jIndexBuffer
{
	unsigned int BufferID = 0;

	virtual void Bind(const jShader* shader) const override;
};

struct jShader_OpenGL : public jShader
{
	unsigned int program = 0;
};

struct jTexture_OpenGL : public jTexture
{
	ETextureType TextureType;
	uint32 TextureID = 0;
};

struct jRenderTarget_OpenGL : public jRenderTarget
{
	std::vector<uint32> fbos;
	std::vector<uint32> rbos;
	std::vector<uint32> drawBuffers;

	uint32 mrt_fbo = 0;
	uint32 mrt_rbo = 0;
	std::vector<uint32> mrt_drawBuffers;

	virtual bool Begin(int index = 0, bool mrt = false) const override;
	virtual void End() const override;
};

struct jUniformBufferBlock_OpenGL : public IUniformBufferBlock
{
	using IUniformBufferBlock::IUniformBufferBlock;
	using IUniformBufferBlock::UpdateBufferData;
	virtual ~jUniformBufferBlock_OpenGL()
	{
		glDeleteBuffers(1, &UBO);
	}

	uint32 BindingPoint = -1;
	uint32 UBO = -1;
	virtual void Init() override;
	virtual void Bind(const jShader* shader) const override;
	virtual void UpdateBufferData(const void* newData, int32 size) override;
	virtual void ClearBuffer(int32 clearValue) override;
};

struct jShaderStorageBufferObject_OpenGL : public IShaderStorageBufferObject
{
	using IShaderStorageBufferObject::IShaderStorageBufferObject;
	using IShaderStorageBufferObject::GetBufferData;
	using IShaderStorageBufferObject::UpdateBufferData;

	virtual ~jShaderStorageBufferObject_OpenGL()
	{
		glDeleteBuffers(1, &SSBO);
	}

	uint32 BindingPoint = -1;
	uint32 SSBO = -1;
	virtual void Init() override;
	virtual void Bind(const jShader* shader) const override;
	virtual void UpdateBufferData(void* newData, int32 size) override;
	virtual void ClearBuffer(int32 clearValue) override;
	virtual void GetBufferData(void* newData, int32 size) override;
};

struct jAtomicCounterBuffer_OpenGL : public IAtomicCounterBuffer
{
	using IAtomicCounterBuffer::IAtomicCounterBuffer;
	virtual ~jAtomicCounterBuffer_OpenGL()
	{
		glDeleteBuffers(1, &ACBO);
	}

	uint32 ACBO = -1;
	virtual void Init() override;
	virtual void Bind(const jShader* shader) const override;
	virtual void UpdateBufferData(void* newData, int32 size) override;
	using IAtomicCounterBuffer::GetBufferData;
	virtual void GetBufferData(void* newData, int32 size) override;
	virtual void ClearBuffer(int32 clearValue) override;
};

class jRHI_OpenGL : public jRHI
{
public:
	jRHI_OpenGL();
	~jRHI_OpenGL();

	virtual jVertexBuffer* CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData) override;


	virtual jIndexBuffer* CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData) override;


	virtual void BindVertexBuffer(const jVertexBuffer* vb, const jShader* shader) override;


	virtual void BindIndexBuffer(const jIndexBuffer* ib, const jShader* shader) override;


	virtual void MapBufferdata(IBuffer* buffer) override;


	virtual void DrawArray(EPrimitiveType type, int vertStartIndex, int vertCount) override;


	virtual void DrawElement(EPrimitiveType type, int elementSize, int32 startIndex = -1, int32 count = -1) override;

	virtual void DispatchCompute(uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ) override;

	virtual void EnableDepthBias(bool enable) override;
	virtual void SetDepthBias(float constant, float slope) override;

	unsigned int GetPrimitiveType(EPrimitiveType type)
	{
		unsigned int primitiveType = 0;
		switch (type)
		{
		case EPrimitiveType::LINES:
			primitiveType = GL_LINES;
			break;
		case EPrimitiveType::TRIANGLES:
			primitiveType = GL_TRIANGLES;
			break;
		case EPrimitiveType::TRIANGLE_STRIP:
			primitiveType = GL_TRIANGLE_STRIP;
			break;
		}
		return primitiveType;
	}


	virtual void SetClear(ERenderBufferType typeBit) override;


	virtual void SetClearColor(float r, float g, float b, float a) override;
	virtual void SetClearColor(Vector4 rgba) override;

	virtual void SetShader(const jShader* shader) override;


	virtual jShader* CreateShader(const jShaderInfo& shaderInfo) override;


	virtual jTexture* CreateNullTexture() const override;
	virtual jTexture* CreateTextureFromData(unsigned char* data, int32 width, int32 height) override;


	virtual bool SetUniformbuffer(const IUniformBuffer* buffer, const jShader* shader) override;


	virtual void SetMatetrial(jMaterialData* materialData, const jShader* shader, int32 baseBindingIndex = 0) override;


	virtual void SetTexture(int32 index, const jTexture* texture) override;


	virtual void SetTextureFilter(ETextureType type, ETextureFilterTarget target, ETextureFilter filter) override;


	virtual void EnableCullFace(bool enable) override;


	virtual jRenderTarget* CreateRenderTarget(const jRenderTargetInfo& info) override;


	virtual void EnableDepthTest(bool enable) override;


	virtual void SetRenderTarget(const jRenderTarget* rt, int32 index = 0, bool mrt = false) override;
	virtual void SetDrawBuffers(const std::initializer_list<EDrawBufferType>& list) override;


	virtual void UpdateVertexBuffer(jVertexBuffer* vb, const std::shared_ptr<jVertexStreamData>& streamData) override;
	virtual void UpdateVertexBuffer(jVertexBuffer* vb, IStreamParam* streamParam, int32 streamParamIndex) override;


	virtual void EnableBlend(bool enable) override;


	virtual void SetBlendFunc(EBlendSrc src, EBlendDest dest) override;

	virtual void EnableStencil(bool enable) override;

	virtual void SetStencilOpSeparate(EFace face, EStencilOp sFail, EStencilOp dpFail, EStencilOp dpPass) override;

	virtual void SetStencilFunc(EDepthStencilFunc func, int32 ref, uint32 mask) override;
	virtual void SetDepthFunc(EDepthStencilFunc func) override;
	virtual void SetDepthMask(bool enable) override;
	virtual void SetColorMask(bool r, bool g, bool b, bool a) override;

	virtual IUniformBufferBlock* CreateUniformBufferBlock(const char* blockname) const override;

	virtual void DrawElementBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex) override;


	virtual void EnableSRGB(bool enable) override;


	virtual IShaderStorageBufferObject* CreateShaderStorageBufferObject(const char* blockname) const override;
	virtual IAtomicCounterBuffer* CreateAtomicCounterBuffer(const char* name, int32 bindingPoint) const override;

	virtual void SetViewport(int32 x, int32 y, int32 width, int32 height) const override;
	virtual void SetViewport(const jViewport& viewport) const override;
	virtual void SetViewportIndexed(int32 index, float x, float y, float width, float height) const override;
	virtual void SetViewportIndexed(int32 index, const jViewport& viewport) const override;
	virtual void SetViewportIndexedArray(int32 startIndex, int32 count, const jViewport* viewports) const override;
};

