﻿#pragma once
#include "jLight.h"

struct jPointLightUniformBufferData
{
    Vector Position;
    float SpecularPow = 0.0f;

    Vector Color;
    float MaxDistance = 0.0f;

    Vector DiffuseIntensity;
    float padding0;

    Vector SpecularIntensity;
    float padding1;

    Matrix ShadowVP[6];

    bool operator == (const jPointLightUniformBufferData& rhs) const
    {
        return (Position == rhs.Position && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity
            && SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow && MaxDistance == rhs.MaxDistance
            && !memcmp(ShadowVP, rhs.ShadowVP, sizeof(ShadowVP)));
    }

    bool operator != (const jPointLightUniformBufferData& rhs) const
    {
        return !(*this == rhs);
    }
};

class jPointLight : public jLight
{
public:
    static constexpr int32 SM_Width = 512;
    static constexpr int32 SM_Height = 512;
    static constexpr float SM_NearDist = 10.0f;
    static constexpr float SM_FarDist = 500.0f;

    jPointLight();
    virtual ~jPointLight();

    void Initialize(const Vector& InPos, const Vector& InColor, float InMaxDist, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower);

    jPointLightUniformBufferData LightData;
    IUniformBufferBlock* LightDataUniformBlock = nullptr;

    virtual bool IsOmnidirectional() const override { return true; }
    virtual void Update(float deltaTime) override;
    virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }
    virtual jCamera* GetLightCamra(int index = 0) const;
    virtual const Matrix* GetLightWorldMatrix() const override;
    virtual jShaderBindingInstance* PrepareShaderBindingInstance(jTexture* InShadowMap) const override;

    FORCEINLINE const jPointLightUniformBufferData& GetLightData() const { return LightData; }

    jCamera* Camera[6] = {0,};
    Matrix LightWorldMatrix;
};