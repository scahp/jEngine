#pragma once
#include "jLight.h"

struct jSpotLightUniformBufferData
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

    Matrix ShadowVP;

    bool operator == (const jSpotLightUniformBufferData& rhs) const
    {
        return (Position == rhs.Position && Direction == rhs.Direction && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity
            && SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow && MaxDistance == rhs.MaxDistance
            && PenumbraRadian == rhs.PenumbraRadian && UmbraRadian == rhs.UmbraRadian && (ShadowVP == rhs.ShadowVP));
    }

    bool operator != (const jSpotLightUniformBufferData& rhs) const
    {
        return !(*this == rhs);
    }
};

class jSpotLight : public jLight
{
public:
    static constexpr int32 SM_Width = 512;
    static constexpr int32 SM_Height = 512;
    static constexpr float SM_NearDist = 10.0f;
    static constexpr float SM_FarDist = 1000.0f;

    jSpotLight();
    virtual ~jSpotLight();

    void Initialize(const Vector& InPos, const Vector& InDirection, const Vector& InColor, float InMaxDistance
        , float InPenumbraRadian, float InUmbraRadian, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower);

    jSpotLightUniformBufferData LightData;
    IUniformBufferBlock* LightDataUniformBlock = nullptr;

    virtual bool IsOmnidirectional() const { return false; }
    virtual void Update(float deltaTime) override;
    virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }
    virtual jCamera* GetLightCamra(int index = 0) const;
    virtual jShaderBindingInstance* PrepareShaderBindingInstance(jTexture* InShadowMap) const override;

    FORCEINLINE const jSpotLightUniformBufferData& GetLightData() const { return LightData; }

    jCamera* Camera = nullptr;
};
