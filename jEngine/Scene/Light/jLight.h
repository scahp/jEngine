#pragma once
#include "Math/Vector.h"
#include "Scene/jCamera.h"

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
			UniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("ShadowMapBlock"), jLifeTimeType::MultiFrame);
		}

		jShadowMapArrayData(const char* prefix)
		{
			if (prefix)
				UniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic(std::string(prefix) + "ShadowMapBlock"), jLifeTimeType::MultiFrame);
			else
				UniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("ShadowMapBlock"), jLifeTimeType::MultiFrame);
		}

		~jShadowMapArrayData()
		{
			delete UniformBlock;
		}

		FORCEINLINE bool IsValid() const { return (ShadowMapCamera[0] && ShadowMapCamera[1] && ShadowMapCamera[2] && ShadowMapCamera[3] && ShadowMapCamera[4] && ShadowMapCamera[5]); }

		jCamera* ShadowMapCamera[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		IUniformBufferBlock* UniformBlock = nullptr;
	};

	static jShadowMapArrayData* CreateShadowMapArray(const Vector& pos);

	//////////////////////////////////////////////////////////////////////////
	//struct jShadowMapData
	//{
	//	FORCEINLINE bool IsValid() const { return (ShadowMapCamera); }

	//	jCamera* ShadowMapCamera = nullptr;

	//	// todo 정리 필요
	//	Matrix CascadeLightVP[NUM_CASCADES];
	//	float CascadeEndsW[NUM_CASCADES] = { 0, };
	//};

	//static jShadowMapData* CreateShadowMap(const Vector& direction, const Vector& pos);
	//static jShadowMapData* CreateCascadeShadowMap(const Vector& direction, const Vector& pos);
}

class jLight
{
public:
	// Create light
	static jLight* CreateAmbientLight(const Vector& color, const Vector& intensity);
	static jDirectionalLight* CreateDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jCascadeDirectionalLight* CreateCascadeDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jPointLight* CreatePointLight(const Vector& pos, const Vector& color, float maxDistance, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jSpotLight* CreateSpotLight(const Vector& pos, const Vector& direction, const Vector& color, float maxDistance
		, float penumbraRadian, float umbraRadian, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower);

	jLight() = default;
	jLight(ELightType type) :Type(type) {}
	virtual ~jLight() {}

	virtual void Update(float deltaTime) { }
	virtual IUniformBufferBlock* GetUniformBufferBlock() const { return nullptr; }

	virtual const jCamera* GetLightCamra(int32 index = 0) const { return nullptr; }
	virtual const jTexture* GetShadowMap(int32 index = 0) const { return nullptr; };

	const ELightType Type = ELightType::MAX;
	jObject* LightDebugObject = nullptr;
	virtual jShaderBindingInstance* PrepareShaderBindingInstance(jTexture* InShadowMap) const { return nullptr; }
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
};

//class jCascadeDirectionalLight : public jDirectionalLight
//{
//public:
//	jCascadeDirectionalLight()
//	{
//		char szTemp[128] = { 0 };
//		for (int32 i = 0; i < NUM_CASCADES; ++i)
//		{
//			sprintf_s(szTemp, sizeof(szTemp), "CascadeLightVP[%d]", i);
//			CascadeLightVP[i].Name = jName(szTemp);
//			sprintf_s(szTemp, sizeof(szTemp), "CascadeEndsW[%d]", i);
//			CascadeEndsW[i].Name = jName(szTemp);
//		}
//	}
//	virtual void Update(float deltaTime) override;
//
//	jUniformBuffer<Matrix> CascadeLightVP[NUM_CASCADES];
//	jUniformBuffer<float> CascadeEndsW[NUM_CASCADES];
//};

class jPointLight : public jLight
{
public:
	jPointLight()
		: jLight(ELightType::POINT)
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("PointLightBlock"), jLifeTimeType::MultiFrame);

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
		delete ShadowMapData;
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

	virtual jCamera* GetLightCamra(int index = 0) const;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;

	virtual void Update(float deltaTime) override;

	//virtual void SetupUniformBuffer() override;

	virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }

	jUniformBuffer<Matrix> OmniShadowMapVP[6];
};

class jSpotLight : public jLight
{
public:
	jSpotLight()
		: jLight(ELightType::SPOT)
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("SpotLightBlock"), jLifeTimeType::MultiFrame);

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
		delete ShadowMapData;
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

	virtual jCamera* GetLightCamra(int index = 0) const;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;

	virtual void Update(float deltaTime) override;

	//virtual void SetupUniformBuffer() override;

	virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }

	jUniformBuffer<Matrix> OmniShadowMapVP[6];
};