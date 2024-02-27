#pragma once
#include "jLight.h"

struct jPathTracingLightUniformBufferData
{
	enum ELightType
	{
		Rect,
		Sphere,
		DistantLight
	};

	Vector position;
	float radius;
	Vector emission;
	float area;
	Vector u;
	int32 type;
	Vector v;

	bool operator == (const jPathTracingLightUniformBufferData& rhs) const
	{
		return (position == rhs.position && emission == rhs.emission && u == rhs.u
			&& v == rhs.v && radius == rhs.radius && area == rhs.area && type == rhs.type);
	}

	bool operator != (const jPathTracingLightUniformBufferData& rhs) const
	{
		return !(*this == rhs);
	}
};

class jPathTracingLight : public jLight
{
public:
	jPathTracingLight() : jLight(ELightType::PATH_TRACING) {}
    void Initialize(const jPathTracingLightUniformBufferData& InData);

	virtual const std::shared_ptr<jShaderBindingInstance>& PrepareShaderBindingInstance(jTexture* InShadowMap) override;

    FORCEINLINE const jPathTracingLightUniformBufferData& GetLightData() const { return LightData; }

private:
    jPathTracingLightUniformBufferData LightData;
    std::shared_ptr<IUniformBufferBlock> LightDataUniformBufferPtr;
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstanceDataPtr;
    bool IsNeedToUpdateShaderBindingInstance = true;                // 위치가 달라지는 경우도 업데이트 되도록... 업데이트 규칙을 좀 만들어야 함
};
