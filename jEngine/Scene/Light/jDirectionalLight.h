#pragma once
#include "jLight.h"

struct jDirectionalLightUniformBufferData
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

    bool operator == (const jDirectionalLightUniformBufferData& rhs) const
    {
        return (Direction == rhs.Direction && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity
            && SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow && ShadowVP == rhs.ShadowVP
            && ShadowV == rhs.ShadowV && LightPos == rhs.LightPos && ShadowMapSize == rhs.ShadowMapSize
            && Near == rhs.Near && Far == rhs.Far);
    }

    bool operator != (const jDirectionalLightUniformBufferData& rhs) const
    {
        return !(*this == rhs);
    }
};

class jDirectionalLight : public jLight
{
public:
    static constexpr int32 SM_Width = 512 * 6;
    static constexpr int32 SM_Height = 512 * 6;
    static constexpr float SM_NearDist = 10.0f;
    static constexpr float SM_FarDist = 20000.0f;
    static constexpr float SM_PosDist = 10000.0f;

	jDirectionalLight();
	virtual ~jDirectionalLight();

    void Initialize(const Vector& InDirection, const Vector& InColor, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower);

    virtual void Update(float deltaTime) override;
	virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }
    virtual const jCamera* GetLightCamra(int32 index = 0) const override;
    virtual const std::shared_ptr<jShaderBindingInstance>& PrepareShaderBindingInstance(jTexture* InShadowMap) override;

    FORCEINLINE const jDirectionalLightUniformBufferData& GetLightData() const { return LightData; }
    FORCEINLINE jDirectionalLightUniformBufferData& GetLightData() { return LightData; }

private:
    jCamera* Camera = nullptr;
    jDirectionalLightUniformBufferData LightData;
    IUniformBufferBlock* LightDataUniformBlock = nullptr;

    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstanceOnlyLightData;
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstanceWithShadowMap;
    bool IsNeedToUpdateShaderBindingInstance = true;
    jTexture* LastUsedShadowMap = nullptr;
};
