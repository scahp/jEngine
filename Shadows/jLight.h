#pragma once
#include "Math\Vector.h"
#include "jRHI.h"
#include "jRHI_OpenGL.h"
#include "jSamplerStatePool.h"

enum class ELightType
{
	AMBIENT = 0,
	DIRECTIONAL,
	POINT,
	SPOT,	
	MAX
};

struct jShader;
struct jTexture;
class jCamera;
struct jFrameBuffer;
struct jMaterialData;
class jDirectionalLight;
class jCascadeDirectionalLight;
class jPointLight;
class jSpotLight;
class jObject;
class jLight;

namespace jLightUtil
{
	void MakeDirectionalLightViewInfo(Vector& outPos, Vector& outTarget, Vector& outUp, const Vector& direction);
	void MakeDirectionalLightViewInfoWithPos(Vector& outTarget, Vector& outUp, const Vector& pos, const Vector& direction);

	//////////////////////////////////////////////////////////////////////////
	struct jShadowMapArrayData
	{
		jShadowMapArrayData()
		{
			UniformBlock = g_rhi->CreateUniformBufferBlock("ShadowMapBlock");
		}

		jShadowMapArrayData(const char* prefix)
		{
			if (prefix)
				UniformBlock = g_rhi->CreateUniformBufferBlock((std::string(prefix) + "ShadowMapBlock").c_str());
			else
				UniformBlock = g_rhi->CreateUniformBufferBlock("ShadowMapBlock");
		}

		~jShadowMapArrayData()
		{
			delete UniformBlock;
		}

		FORCEINLINE bool IsValid() const { return (ShadowMapFrameBuffer && ShadowMapCamera[0] && ShadowMapCamera[1] && ShadowMapCamera[2] && ShadowMapCamera[3] && ShadowMapCamera[4] && ShadowMapCamera[5]); }

		std::shared_ptr<jFrameBuffer> ShadowMapFrameBuffer;
		std::shared_ptr<jSamplerStateInfo> ShadowMapSamplerState;
		jCamera* ShadowMapCamera[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		IUniformBufferBlock* UniformBlock = nullptr;
	};

	static jShadowMapArrayData* CreateShadowMapArray(const Vector& pos);

	//////////////////////////////////////////////////////////////////////////
	struct jShadowMapData
	{
		jShadowMapData()
		{
			//UniformBlock = g_rhi->CreateUniformBufferBlock("ShadowMapBlock");
		}

		jShadowMapData(const char* prefix)
		{
			//if (prefix)
			//	UniformBlock = g_rhi->CreateUniformBufferBlock((std::string(prefix) + "ShadowMapBlock").c_str());
			//else
			//	UniformBlock = g_rhi->CreateUniformBufferBlock("ShadowMapBlock");
		}

		~jShadowMapData()
		{
			delete UniformBlock;
		}

		FORCEINLINE bool IsValid() const { return (ShadowMapCamera && ShadowMapFrameBuffer); }

		std::shared_ptr<jFrameBuffer> ShadowMapFrameBuffer;
		std::shared_ptr<jSamplerStateInfo> ShadowMapSamplerState;
		jCamera* ShadowMapCamera = nullptr;
		IUniformBufferBlock* UniformBlock = nullptr;

		// todo 정리 필요
		Matrix CascadeLightVP[NUM_CASCADES];
		float CascadeEndsW[NUM_CASCADES] = { 0, };
	};

	static jShadowMapData* CreateShadowMap(const Vector& direction, const Vector& pos);
	static jShadowMapData* CreateCascadeShadowMap(const Vector& direction, const Vector& pos);
}

typedef std::function<void(const jFrameBuffer*, int32, const jCamera*, const std::vector<jViewport>& viewports)> RenderToShadowMapFunc;

class jLight
{
public:
	// todo debug object
	static jLight* CreateAmbientLight(const Vector& color, const Vector& intensity);
	static jDirectionalLight* CreateDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jCascadeDirectionalLight* CreateCascadeDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jPointLight* CreatePointLight(const Vector& pos, const Vector& color, float maxDistance, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jSpotLight* CreateSpotLight(const Vector& pos, const Vector& direction, const Vector& color, float maxDistance
		, float penumbraRadian, float umbraRadian, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower);

	static void BindLights(const std::list<const jLight*>& lights, const jShader* shader);

	jLight() = default;
	jLight(ELightType type) :Type(type) {}
	virtual ~jLight() {}

	virtual void BindLight(const jShader* shader) const {}
	virtual void GetMaterialData(jMaterialData* OutMaterialData) const {}
	virtual const jMaterialData* GetMaterialData() const { return nullptr; }
	virtual jFrameBuffer* GetShadowMapFrameBuffer() const { return nullptr; }
	virtual std::shared_ptr<jFrameBuffer> GetShadowMapFrameBufferPtr() const { return nullptr; }
	virtual jCamera* GetLightCamra(int index = 0) const { return nullptr; }
	virtual void RenderToShadowMap(const RenderToShadowMapFunc& func, const jShader* shader) const {}
	virtual void Update(float deltaTime) { }
	virtual IUniformBufferBlock* GetUniformBufferBlock() const { return nullptr; }

	const ELightType Type = ELightType::MAX;
	jObject* LightDebugObject = nullptr;
	std::vector<jViewport> Viewports;
	bool DirtyMaterialData = true;

	jShaderBindingInstance* ShaderBindingInstance = nullptr;
	bool NeedUpdateRenderObjectUniformBuffer = false;
	void GetShaderBindingInstance(std::vector<const jShaderBindingInstance*>& OutShaderBindingInstance)
	{
		OutShaderBindingInstance.push_back(ShaderBindingInstance);
	}
	virtual void PrepareShaderBindingInstance() {}
};

class jAmbientLight : public jLight
{
public:
	jAmbientLight() : jLight(ELightType::AMBIENT) {}

	struct LightData
	{
		Vector Color;
		Vector Intensity;
	};
	
	LightData Data;
	
	virtual void BindLight(const jShader* shader) const override;

};

class jDirectionalLight : public jLight
{
public:
	jDirectionalLight()
		: jLight(ELightType::DIRECTIONAL)
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock("DirectionalLightBlock", sizeof(LightData));
	}
	virtual ~jDirectionalLight()
	{
		delete LightDataUniformBlock;
	}

	struct LightData
	{
		Vector Direction;
		float SpecularPow = 0.0f;

		Vector Color;
		float padding0;

		Vector DiffuseIntensity;
		float padding1;

		Vector SpecularIntensity;
		float padding2;

		Matrix ShadowVP;
		Matrix ShadowV;
		
		Vector LightPos;
		float padding3;

		Vector2 ShadowMapSize;
		float Near;
		float Far;

		bool operator == (const LightData& rhs) const
		{
			return (Direction == rhs.Direction && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity 
				&& SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow && ShadowVP == rhs.ShadowVP
				&& ShadowV == rhs.ShadowV && LightPos == rhs.LightPos && ShadowMapSize == rhs.ShadowMapSize 
				&& Near == rhs.Near && Far == rhs.Far);
		}

		bool operator != (const LightData& rhs) const
		{
			return !(*this == rhs);
		}
	};

	LightData Data;
	IUniformBufferBlock* LightDataUniformBlock = nullptr;
	std::shared_ptr<jRenderTarget> ShadowMapPtr;

	virtual void BindLight(const jShader* shader) const override;
	virtual void GetMaterialData(jMaterialData* OutMaterialData) const override;
	virtual const jMaterialData* GetMaterialData() const override { return &MaterialData; }
	virtual jFrameBuffer* GetShadowMapFrameBuffer() const override;
	virtual std::shared_ptr<jFrameBuffer> GetShadowMapFrameBufferPtr() const override;
	virtual jCamera* GetLightCamra(int index = 0) const;

	jLightUtil::jShadowMapData* ShadowMapData = nullptr;
	jTexture* GetShadowMap() const;

	virtual void RenderToShadowMap(const RenderToShadowMapFunc& func, const jShader* shader) const override;
	virtual void Update(float deltaTime) override;
	virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }

	jMaterialData MaterialData;
	void UpdateMaterialData();

	//////////////////////////////////////////////////////////////////////////	
	virtual void PrepareShaderBindingInstance() override
	{
		int32 BindingPoint = 0;
		std::vector<TShaderBinding<IUniformBufferBlock*>> UniformBinding;
		UniformBinding.reserve(2);
		UniformBinding.emplace_back(TShaderBinding(BindingPoint++, EShaderAccessStageFlag::ALL_GRAPHICS, LightDataUniformBlock));

		//std::vector<TShaderBinding<jTextureBindings>> TextureBinding;
		//for (int32 i = 0; i < (int32)MaterialData.Params.size(); ++i)
		//{
		//	if (!MaterialData.Params[i].Texture)
		//		continue;

		//	jTextureBindings bindings;
		//	bindings.Texture = MaterialData.Params[i].Texture;
		//	bindings.SamplerState = MaterialData.Params[i].SamplerState;
		//	TextureBinding.push_back(TShaderBinding(BindingPoint++, EShaderAccessStageFlag::ALL_GRAPHICS, bindings));
		//}

		std::vector<TShaderBinding<jTextureBindings>> TextureBinding;
		if (ShadowMapPtr)
		{
			jTextureBindings bindings;
			bindings.Texture = ShadowMapPtr->GetTexture();
			bindings.SamplerState = nullptr;
			TextureBinding.emplace_back(TShaderBinding(BindingPoint++, EShaderAccessStageFlag::ALL_GRAPHICS, bindings));
		}		

		if (NeedUpdateRenderObjectUniformBuffer)
		{
			delete ShaderBindingInstance;
			NeedUpdateRenderObjectUniformBuffer = false;
		}

		if (ShaderBindingInstance)
		{
			ShaderBindingInstance->UniformBuffers.clear();
			ShaderBindingInstance->UniformBuffers.push_back(LightDataUniformBlock);

			ShaderBindingInstance->Textures.resize(TextureBinding.size());
			for (int32 i = 0; i < (int32)TextureBinding.size(); ++i)
				ShaderBindingInstance->Textures[i] = TextureBinding[i].Data;
		}
		else
		{
			ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(UniformBinding, TextureBinding);
			ShaderBindingInstance->UpdateShaderBindings();
		}
	}
	//////////////////////////////////////////////////////////////////////////
};

class jCascadeDirectionalLight : public jDirectionalLight
{
public:
	jCascadeDirectionalLight()
	{
		char szTemp[128] = { 0 };
		for (int32 i = 0; i < NUM_CASCADES; ++i)
		{
			sprintf_s(szTemp, sizeof(szTemp), "CascadeLightVP[%d]", i);
			CascadeLightVP[i].Name = jName(szTemp);
			sprintf_s(szTemp, sizeof(szTemp), "CascadeEndsW[%d]", i);
			CascadeEndsW[i].Name = jName(szTemp);
		}
	}
	virtual void RenderToShadowMap(const RenderToShadowMapFunc& func, const jShader* shader) const override;
	virtual void Update(float deltaTime) override;
	virtual void BindLight(const jShader* shader) const override;

	jUniformBuffer<Matrix> CascadeLightVP[NUM_CASCADES];
	jUniformBuffer<float> CascadeEndsW[NUM_CASCADES];
};

class jPointLight : public jLight
{
public:
	jPointLight()
		: jLight(ELightType::POINT) 
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock("PointLightBlock");
		
		char szTemp[128] = { 0, };
		for (int i = 0; i < 6; ++i)
		{
			sprintf_s(szTemp, sizeof(szTemp), "OmniShadowMapVP[%d]", i);
			OmniShadowMapVP[i].Name = jName(szTemp);
		}
	}

	virtual ~jPointLight()
	{
		delete LightDataUniformBlock;
	}

	struct LightData
	{
		Vector Position;
		float SpecularPow = 0.0f;

		Vector Color;
		float MaxDistance = 0.0f;

		Vector DiffuseIntensity;
		float padding0;
		
		Vector SpecularIntensity;		
		float padding1;

		bool operator == (const LightData& rhs) const
		{
			return (Position == rhs.Position && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity
				&& SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow && MaxDistance == rhs.MaxDistance);
		}

		bool operator != (const LightData& rhs) const
		{
			return !(*this == rhs);
		}
	};

	LightData Data;
	IUniformBufferBlock* LightDataUniformBlock = nullptr;

	virtual void BindLight(const jShader* shader) const override;
	virtual void GetMaterialData(jMaterialData* OutMaterialData) const override;
	virtual const jMaterialData* GetMaterialData() const override { return &MaterialData; }
	virtual jFrameBuffer* GetShadowMapFrameBuffer() const override;
	virtual std::shared_ptr<jFrameBuffer> GetShadowMapFrameBufferPtr() const override;
	virtual jCamera* GetLightCamra(int index = 0) const;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;

	virtual void RenderToShadowMap(const RenderToShadowMapFunc& func, const jShader* shader) const override;
	virtual void Update(float deltaTime) override;
	virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }

	jMaterialData MaterialData;
	void UpdateMaterialData();

	jUniformBuffer<Matrix> OmniShadowMapVP[6];
};

class jSpotLight : public jLight
{
public:
	jSpotLight()
		: jLight(ELightType::SPOT)
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock("SpotLightBlock");

		char szTemp[128] = { 0, };
		for (int i = 0; i < 6; ++i)
		{
			sprintf_s(szTemp, sizeof(szTemp), "OmniShadowMapVP[%d]", i);
			OmniShadowMapVP[i].Name = jName(szTemp);
		}
	}

	virtual ~jSpotLight()
	{
		delete LightDataUniformBlock;
	}

	struct LightData
	{
		Vector Position;
		float MaxDistance = 0.0f;

		Vector Direction;
		float PenumbraRadian = 0.0f;

		Vector Color;
		float UmbraRadian = 0.0f;

		Vector DiffuseIntensity;
		float SpecularPow = 0.0f;

		Vector SpecularIntensity;
		float padding0;

		bool operator == (const LightData& rhs) const
		{
			return (Position == rhs.Position && Direction == rhs.Direction && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity
				&& SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow && MaxDistance == rhs.MaxDistance
				&& PenumbraRadian == rhs.PenumbraRadian && UmbraRadian == rhs.UmbraRadian);
		}

		bool operator != (const LightData& rhs) const
		{
			return !(*this == rhs);
		}
	};

	LightData Data;
	IUniformBufferBlock* LightDataUniformBlock = nullptr;

	virtual void BindLight(const jShader* shader) const override;
	virtual void GetMaterialData(jMaterialData* OutMaterialData) const override;
	virtual const jMaterialData* GetMaterialData() const override { return &MaterialData; }
	virtual jFrameBuffer* GetShadowMapFrameBuffer() const override;
	virtual std::shared_ptr<jFrameBuffer> GetShadowMapFrameBufferPtr() const override;
	virtual jCamera* GetLightCamra(int index = 0) const;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;

	virtual void RenderToShadowMap(const RenderToShadowMapFunc& func, const jShader* shader) const override;
	virtual void Update(float deltaTime) override;
	virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }

	jMaterialData MaterialData;
	void UpdateMaterialData();

	jUniformBuffer<Matrix> OmniShadowMapVP[6];
};