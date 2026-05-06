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
    float SM_FarDist = 1000.0f;

    jSpotLight();
    virtual ~jSpotLight();

    void Initialize(const Vector& InPos, const Vector& InDirection, const Vector& InColor, float InMaxDistance
        , float InPenumbraRadian, float InUmbraRadian, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower);

    virtual bool IsOmnidirectional() const { return false; }
    virtual void Update(float deltaTime) override;
    virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlockPtr.get(); }
    virtual jCamera* GetLightCamra(int index = 0) const;
    virtual const Matrix* GetLightWorldMatrix() const override;
    virtual const std::shared_ptr<jShaderBindingInstance>& PrepareShaderBindingInstance(jTexture* InShadowMap) override;
    virtual bool IsUseRevereZPerspective() const { return true; }
    virtual bool SupportsPosition() const override { return true; }
    virtual bool SupportsDirection() const override { return true; }
    virtual Vector GetPosition() const override { return LightData.Position; }
    virtual Vector GetDirection() const override { return LightData.Direction; }

    FORCEINLINE const jSpotLightUniformBufferData& GetLightData() const { return LightData; }

    virtual void SetPosition(const Vector& InPosition) override
    {
        if (LightData.Position != InPosition)
        {
            LightData.Position = InPosition;
            IsNeedToUpdateShaderBindingInstance = true;
        }
    }

    jCamera* Camera = nullptr;
    Matrix LightWorldMatrix;

    std::shared_ptr<IUniformBufferBlock> LightDataUniformBlockPtr;
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstanceOnlyLightData;
    std::shared_ptr<jShaderBindingInstance> ShaderBindingInstanceWithShadowMap;
    bool IsNeedToUpdateShaderBindingInstance = true;                // 위치 변경 시에도 shader binding instance를 갱신한다.

    virtual void SetDirection(const Vector& InDirection) override
    {
        if (LightData.Direction != InDirection)
        {
            LightData.Direction = InDirection;
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
            SM_FarDist = InMaxDistance;
            IsNeedToUpdateShaderBindingInstance = true;
        }
    }

    void SetConeAngles(float InPenumbraRadian, float InUmbraRadian)
    {
        if (LightData.PenumbraRadian != InPenumbraRadian || LightData.UmbraRadian != InUmbraRadian)
        {
            LightData.PenumbraRadian = InPenumbraRadian;
            LightData.UmbraRadian = InUmbraRadian;
            IsNeedToUpdateShaderBindingInstance = true;
        }
    }

private:
    jSpotLightUniformBufferData LightData;
};
