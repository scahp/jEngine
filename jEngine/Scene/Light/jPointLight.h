#pragma once
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

    virtual bool IsOmnidirectional() const override { return true; }
    virtual void Update(float deltaTime) override;
    virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlockPtr.get(); }
    virtual jCamera* GetLightCamra(int index = 0) const;
    virtual const Matrix* GetLightWorldMatrix() const override;
    virtual const std::shared_ptr<jShaderBindingInstance>& PrepareShaderBindingInstance(jTexture* InShadowMap) override;
    virtual bool SupportsPosition() const override { return true; }
    virtual Vector GetPosition() const override { return LightData.Position; }

    FORCEINLINE const jPointLightUniformBufferData& GetLightData() const { return LightData; }

    virtual void SetPosition(const Vector& InPosition) override
    {
        if (LightData.Position != InPosition)
        {
            LightData.Position = InPosition;
            IsNeedToUpdateShaderBindingInstance = true;
        }
    }

    void SetColor(const Vector& InColor)
    {
        if (LightData.Color != InColor)
        {
            LightData.Color = InColor;
            IsNeedToUpdateShaderBindingInstance = true;
        }
    }

    void SetMaxDistance(float InMaxDistance)
    {
        if (LightData.MaxDistance != InMaxDistance)
        {
            LightData.MaxDistance = InMaxDistance;
            IsNeedToUpdateShaderBindingInstance = true;
        }
    }

    jCamera* Camera[6] = {0,};
    Matrix LightWorldMatrix;

    std::shared_ptr<IUniformBufferBlock> LightDataUniformBlockPtr;

    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstanceOnlyLightData;
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstanceWithShadowMap;
    bool IsNeedToUpdateShaderBindingInstance = true;                // 위치 변경 시에도 shader binding instance를 갱신한다.
    jTexture* LastUsedShadowMap = nullptr;

private:
    jPointLightUniformBufferData LightData;
};
