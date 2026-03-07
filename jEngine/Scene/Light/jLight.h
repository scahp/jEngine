#pragma once
#include "Math/Vector.h"
#include "Scene/jCamera.h"

enum class ELightType
{
	AMBIENT = 0,
	DIRECTIONAL,
	POINT,
	SPOT,
	PATH_TRACING,		// Specific type of light is defined in jPathTracingLight
	MAX
};

struct alignas(16) jHWRTDIPackedLight
{
    Vector4 ColorAndType = Vector4(0.0f, 0.0f, 0.0f, -1.0f);            // xyz: color, w: ELightType
    Vector4 PositionAndMaxDistance = Vector4(0.0f, 0.0f, 0.0f, 0.0f);   // xyz: position, w: max distance
    Vector4 DirectionAndPenumbra = Vector4(0.0f, -1.0f, 0.0f, 0.0f);    // xyz: direction, w: penumbra
    Vector4 UmbraAndPadding = Vector4(0.0f, 0.0f, 0.0f, 0.0f);          // x: umbra
};
static_assert((sizeof(jHWRTDIPackedLight) % 16) == 0, "jHWRTDIPackedLight size must be 16-byte aligned");

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
class jPathTracingLight;
struct jPathTracingLightUniformBufferData;

namespace jLightUtil
{
	void MakeDirectionalLightViewInfo(Vector& outPos, Vector& outTarget, Vector& outUp, const Vector& direction);
	void MakeDirectionalLightViewInfoWithPos(Vector& outTarget, Vector& outUp, const Vector& pos, const Vector& direction);

	//////////////////////////////////////////////////////////////////////////
	struct jShadowMapArrayData
	{
		jShadowMapArrayData()
		{
			UniformBlockPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("ShadowMapBlock"), jLifeTimeType::MultiFrame);
		}

		jShadowMapArrayData(const char* prefix)
		{
			if (prefix)
				UniformBlockPtr = g_rhi->CreateUniformBufferBlock(jNameStatic(std::string(prefix) + "ShadowMapBlock"), jLifeTimeType::MultiFrame);
			else
				UniformBlockPtr = g_rhi->CreateUniformBufferBlock(jNameStatic("ShadowMapBlock"), jLifeTimeType::MultiFrame);
		}

		~jShadowMapArrayData()
		{
		}

		FORCEINLINE bool IsValid() const { return (ShadowMapCamera[0] && ShadowMapCamera[1] && ShadowMapCamera[2] && ShadowMapCamera[3] && ShadowMapCamera[4] && ShadowMapCamera[5]); }

		jCamera* ShadowMapCamera[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		std::shared_ptr<IUniformBufferBlock> UniformBlockPtr;
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

using PRE_UPDATE_LAMBDA = void (*)(class jLight*, float);

class jLight
{
public:
	// Create light
	static jLight* CreateAmbientLight(const Vector& color, const Vector& intensity);
	static jPathTracingLight* CreatePathTracingLight(const jPathTracingLightUniformBufferData& InData);
	static jDirectionalLight* CreateDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jCascadeDirectionalLight* CreateCascadeDirectionalLight(const Vector& direction, const Vector& color, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jPointLight* CreatePointLight(const Vector& pos, const Vector& color, float maxDistance, const Vector& diffuseIntensity
		, const Vector& specularIntensity, float specularPower);
	static jSpotLight* CreateSpotLight(const Vector& pos, const Vector& direction, const Vector& color, float maxDistance
		, float penumbraRadian, float umbraRadian, const Vector& diffuseIntensity, const Vector& specularIntensity, float specularPower);
	
	static const std::vector<jLight*>& GetLights() { return s_Lights; }
	static const void AddLights(jLight* InLight) { return s_Lights.push_back(InLight); }
	static const void RemoveLights(jLight* InLight) 
	{
		std::erase_if(s_Lights, [&InLight](jLight* param)
            {
                return (param == InLight);
            });
	}
	static std::vector<jLight*> s_Lights;

	jLight() = default;
	jLight(ELightType type) :Type(type) {}
	virtual ~jLight();

	virtual bool IsOmnidirectional() const { return false; }
	virtual void Update(float deltaTime)
	{
		if (PreUpdateLambda) PreUpdateLambda(this, deltaTime);
	}
	virtual IUniformBufferBlock* GetUniformBufferBlock() const { return nullptr; }

	virtual const jCamera* GetLightCamra(int32 index = 0) const { return nullptr; }
	virtual const jTexture* GetShadowMap(int32 index = 0) const { return nullptr; };
	virtual bool IsUseRevereZPerspective() const { return false; }
	
	// Light world matrix by using light's Position and MaxDistance
	virtual const Matrix* GetLightWorldMatrix() const { return nullptr; }

	FORCEINLINE ELightType GetLightType() const { return Type; }

	const ELightType Type = ELightType::MAX;
	bool IsShadowCaster = true;
	jObject* LightDebugObject = nullptr;
	virtual const std::shared_ptr<jShaderBindingInstance>& PrepareShaderBindingInstance(jTexture* InShadowMap)
	{ 
		static std::shared_ptr<jShaderBindingInstance> s_Temp;
		return s_Temp;
	}

	PRE_UPDATE_LAMBDA PreUpdateLambda = nullptr;
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
