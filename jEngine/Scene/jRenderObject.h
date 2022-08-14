#pragma once
#include "Math\Vector.h"
#include "Math\Matrix.h"
#include "jBoundPrimitiveType.h"

class jCamera;
class jLight;
struct jVertexBuffer;
struct jIndexBuffer;
struct jShader;
struct jMaterialData;
struct jTexture;
struct jSamplerStateInfo;

enum class EShadingModel : int32
{
	BASE = 0,
	HAIR,
};

class jRenderObject
{
public:
	jRenderObject();
	~jRenderObject();

	void CreateRenderObject(const std::shared_ptr<jVertexStreamData>& vertexStream, const std::shared_ptr<jIndexStreamData>& indexStream);
	void UpdateVertexStream(const std::shared_ptr<jVertexStreamData>& vertexStream);
	void UpdateVertexStream();

	//void Draw(const jCamera* camera, const jShader* shader, int32 startIndex = -1, int32 count = -1);
	void Draw(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader
		, const std::list<const jLight*>& lights, int32 startIndex = 0, int32 count = -1, int32 instanceCount = 1, bool bPoisitionOnly = false);

	// todo 함수를 줄일까? 아니면 이렇게 쓸까? 고민
	//void Draw(const jCamera* camera, const jShader* shader, int32 startIndex, int32 count, int32 baseVertexIndex);
	void DrawBaseVertexIndex(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jCamera* camera, const jShader* shader, const std::list<const jLight*>& lights, const jMaterialData& materialData, int32 startIndex, int32 count, int32 baseVertexIndex, int32 instanceCount = 1);
	
	void SetRenderProperty(const std::shared_ptr<jRenderFrameContext>& InRenderFrameContext, const jShader* shader, bool bPositionOnly = false);
	void SetCameraProperty(const jShader* shader, const jCamera* camera);
	void SetMaterialProperty(const jShader* shader, const jMaterialData* materialData, const std::vector<const jMaterialData*>& dynamicMaterialData);
	void SetLightProperty(const jShader* shader, const jCamera* camera, const std::list<const jLight*>& lights, jMaterialData* materialData);
	//void SetLightProperty(const jShader* shader, const jLight* light, jMaterialData* materialData);
	void SetTextureProperty(const jShader* shader, const jMaterialData* materialData);

	const std::vector<float>& GetVertices() const;

	void UpdateWorldMatrix();
	Matrix GetWorld() const;

	std::shared_ptr<jVertexStreamData> VertexStream;
	std::shared_ptr<jVertexStreamData> VertexStream_PositionOnly;
	jVertexBuffer* VertexBuffer = nullptr;
	jVertexBuffer* VertexBuffer_PositionOnly = nullptr;

	std::shared_ptr<jIndexStreamData> IndexStream;
	jIndexBuffer* IndexBuffer = nullptr;
	jMaterialData MaterialData;
    std::vector<const jMaterialData*> DynamicMaterialData;

	jTexture* tex_object_array = nullptr;
	jSamplerStateInfo* samplerStateTexArray = nullptr;

	// todo 정리 필요.
	int UseUniformColor = 0;
	Vector4 Color = Vector4::OneVector;
	int UseMaterial = 0;
	EShadingModel ShadingModel = EShadingModel::BASE;

	Matrix World;

	bool Collided = false;
	bool IsTwoSided = false;

	bool IsHiddenBoundBox = false;
	
	FORCEINLINE void SetPos(const Vector& InPos) { Pos = InPos; SetDirtyFlags(EDirty::POS); }
	FORCEINLINE void SetRot(const Vector& InRot) { Rot = InRot; SetDirtyFlags(EDirty::ROT); }
	FORCEINLINE void SetScale(const Vector& InScale) { Scale = InScale; SetDirtyFlags(EDirty::SCALE); }
	FORCEINLINE const Vector& GetPos() const { return Pos; }
	FORCEINLINE const Vector& GetRot() const { return Rot; }
	FORCEINLINE const Vector& GetScale() const { return Scale; }

	void SetTexture(int32 index, const jName& name, const jTexture* texture, const jSamplerStateInfo* samplerState = nullptr);
	void SetTextureWithCommonName(int32 index, const jTexture* texture, const jSamplerStateInfo* samplerState = nullptr);
	void ClearTexture();

	struct jRenderObjectUniformBuffer
	{
		Matrix M;
		Matrix MV;
		Matrix MVP;
		Matrix InvM;
	};

	//////////////////////////////////////////////////////////////////////////
	// RenderObjectUniformBuffer
	FORCEINLINE IUniformBufferBlock* CreateRenderObjectUniformBuffer() const
	{
		return g_rhi->CreateUniformBufferBlock("RenderObjectUniformParameters", sizeof(jRenderObjectUniformBuffer));
	}

	void UpdateRenderObjectUniformBuffer(const jView* view);

	void SetupRenderObjectUniformBuffer(const jView* view)
	{
		RenderObjectUniformBuffer = CreateRenderObjectUniformBuffer();

		UpdateRenderObjectUniformBuffer(view);
	}

	IUniformBufferBlock* RenderObjectUniformBuffer = nullptr;
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	std::shared_ptr<jShaderBindingInstance> ShaderBindingInstancePtr;
	bool NeedUpdateRenderObjectUniformBuffer = true;
	void PrepareShaderBindingInstance()
	{
		int32 BindingPoint = 0;
		std::vector<jShaderBinding> ShaderBindings;
		ShaderBindings.reserve(1 + MaterialData.Params.size());
		ShaderBindings.emplace_back(jShaderBinding(BindingPoint++, EShaderBindingType::UNIFORMBUFFER
			, EShaderAccessStageFlag::ALL_GRAPHICS, std::make_shared<jUniformBufferResource>(RenderObjectUniformBuffer)));

        for (int32 i = 0; i < (int32)MaterialData.Params.size(); ++i)
        {
			ShaderBindings.emplace_back(jShaderBinding(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV
				, EShaderAccessStageFlag::ALL_GRAPHICS, std::make_shared<jTextureResource>(MaterialData.Params[i].Texture, MaterialData.Params[i].SamplerState)));
        }

		//if (NeedUpdateRenderObjectUniformBuffer)
		{
			//if (ShaderBindingInstancePtr)
			//{
				//ShaderBindingInstancePtr->UpdateShaderBindings(ShaderBindings);
			//}
			//else
			{
				// delete ShaderBindingInstance;
				ShaderBindingInstancePtr = g_rhi->CreateShaderBindingInstance(ShaderBindings);
				ShaderBindingInstancePtr->UpdateShaderBindings(ShaderBindings);
			}
			//NeedUpdateRenderObjectUniformBuffer = false;
		}
	}
	void GetShaderBindingInstance(std::vector<std::shared_ptr<jShaderBindingInstance>>& OutShaderBindingInstance)
	{
		check(ShaderBindingInstancePtr);
		OutShaderBindingInstance.push_back(ShaderBindingInstancePtr);
	}
	//////////////////////////////////////////////////////////////////////////

private:
	enum EDirty : int8
	{
		NONE	= 0,
		POS		= 1,
		ROT		= 1 << 1,
		SCALE	= 1 << 2,
		POS_ROT_SCALE = POS | ROT | SCALE,
	};
	EDirty DirtyFlags = EDirty::POS_ROT_SCALE;
	void SetDirtyFlags(EDirty InEnum)
	{
		using T = std::underlying_type<EDirty>::type;
		DirtyFlags = static_cast<EDirty>(static_cast<T>(InEnum) | static_cast<T>(DirtyFlags));
	}
	void ClearDirtyFlags(EDirty InEnum)
	{
		using T = std::underlying_type<EDirty>::type;
		DirtyFlags = static_cast<EDirty>(static_cast<T>(InEnum) & (!static_cast<T>(DirtyFlags)));
	}
	FORCEINLINE void ClearDirtyFlags() { DirtyFlags = EDirty::NONE; }

	Vector Pos = Vector::ZeroVector;
	Vector Rot = Vector::ZeroVector;
	Vector Scale = Vector::OneVector;
};

