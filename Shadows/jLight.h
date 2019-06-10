#pragma once
#include "Math\Vector.h"
#include "jRHI.h"
#include "jRHI_OpenGL.h"

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
struct jRenderTarget;
struct jMaterialData;
class jDirectionalLight;
class jPointLight;
class jSpotLight;
class jObject;

namespace jLightUtil
{
	//////////////////////////////////////////////////////////////////////////
	struct jShadowMapArrayData
	{
		FORCEINLINE bool IsValid() const { return (ShadowMapRenderTarget && ShadowMapCamera[0] && ShadowMapCamera[1] && ShadowMapCamera[2] && ShadowMapCamera[3] && ShadowMapCamera[4] && ShadowMapCamera[5]); }

		jRenderTarget* ShadowMapRenderTarget = nullptr;
		jCamera* ShadowMapCamera[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	};

	jShadowMapArrayData* CreateShadowMapArray(const Vector& pos);

	//////////////////////////////////////////////////////////////////////////
	struct jShadowMapData
	{
		FORCEINLINE bool IsValid() const { return (ShadowMapCamera && ShadowMapRenderTarget); }

		jRenderTarget* ShadowMapRenderTarget = nullptr;
		jCamera* ShadowMapCamera = nullptr;
	};

	static jShadowMapData* CreateShadowMap(const Vector& direction, const Vector& pos);
}

class jLight
{
public:
	// todo debug object
	static jLight* CreateAmbientLight(const Vector& color, const Vector& intensity);
	static jDirectionalLight* CreateDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jPointLight* CreatePointLight(const Vector& pos, const Vector& color, float maxDistance, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jSpotLight* CreateSpotLight(const Vector& pos, const Vector& direction, const Vector& color, float maxDistance
		, float penumbraRadian, float umbraRadian, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower);

	jLight() = default;
	jLight(ELightType type) :Type(type) {}
	virtual ~jLight() {}

	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) {}

	const ELightType Type = ELightType::MAX;
	jObject* LightDebugObject = nullptr;
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
	
	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) override;
};

class jDirectionalLight : public jLight
{
public:
	jDirectionalLight()
		: jLight(ELightType::DIRECTIONAL)
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock("DirectionalLightBlock");
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

		bool operator == (const LightData& rhs) const
		{
			return (Direction == rhs.Direction && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity 
				&& SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow);
		}

		bool operator != (const LightData& rhs) const
		{
			return !(*this == rhs);
		}
	};

	LightData Data;
	IUniformBufferBlock* LightDataUniformBlock = nullptr;

	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) override;

	jLightUtil::jShadowMapData* ShadowMapData = nullptr;
	jTexture* GetShadowMap() const;
};

class jPointLight : public jLight
{
public:
	jPointLight()
		: jLight(ELightType::POINT) 
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock("PointLightBlock");
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

	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) override;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;
};

class jSpotLight : public jLight
{
public:
	jSpotLight()
		: jLight(ELightType::SPOT)
	{
		LightDataUniformBlock = g_rhi->CreateUniformBufferBlock("SpotLightBlock");
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

	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) override;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;

};