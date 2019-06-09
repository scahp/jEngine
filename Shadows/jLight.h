#pragma once
#include "Math\Vector.h"

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
	static jDirectionalLight* CreateDirectionalLight(const Vector& direction, const Vector4& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jPointLight* CreatePointLight(const Vector& pos, const Vector4& color, float maxDistance, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jSpotLight* CreateSpotLight(const Vector& pos, const Vector& direction, const Vector4& color, float maxDistance
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
	jDirectionalLight() : jLight(ELightType::DIRECTIONAL) {}

	struct LightData
	{
		Vector Direction;
		Vector4 Color;
		Vector DiffuseIntensity;
		Vector SpecularIntensity;
		float SpecularPow = 0.0f;
	};

	LightData Data;

	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) override;

	jLightUtil::jShadowMapData* ShadowMapData = nullptr;
	jTexture* GetShadowMap() const;
};

class jPointLight : public jLight
{
public:
	jPointLight() : jLight(ELightType::POINT) {}

	struct LightData
	{
		Vector Position;
		Vector4 Color;
		Vector DiffuseIntensity;
		Vector SpecularIntensity;
		float SpecularPow = 0.0f;
		float MaxDistance = 0.0f;
	};

	LightData Data;

	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) override;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;
};

class jSpotLight : public jLight
{
public:
	jSpotLight() : jLight(ELightType::SPOT) {}

	struct LightData
	{
		Vector Position;
		Vector Direction;
		Vector4 Color;
		Vector DiffuseIntensity;
		Vector SpecularIntensity;
		float SpecularPow = 0.0f;
		float MaxDistance = 0.0f;
		float PenumbraRadian = 0.0f;
		float UmbraRadian = 0.0f;
	};

	LightData Data;

	virtual void BindLight(jShader* shader, jMaterialData* materialData, int32 index = 0) override;

	jLightUtil::jShadowMapArrayData* ShadowMapData = nullptr;

};