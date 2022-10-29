#pragma once
#include "jLight.h"

struct jLightDataUniformBuffer
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

    bool operator == (const jLightDataUniformBuffer& rhs) const
    {
        return (Direction == rhs.Direction && Color == rhs.Color && DiffuseIntensity == rhs.DiffuseIntensity
            && SpecularIntensity == rhs.SpecularIntensity && SpecularPow == rhs.SpecularPow && ShadowVP == rhs.ShadowVP
            && ShadowV == rhs.ShadowV && LightPos == rhs.LightPos && ShadowMapSize == rhs.ShadowMapSize
            && Near == rhs.Near && Far == rhs.Far);
    }

    bool operator != (const jLightDataUniformBuffer& rhs) const
    {
        return !(*this == rhs);
    }
};

class jDirectionalLight : public jLight
{
public:
    static constexpr float SM_Width = 512;
    static constexpr float SM_Height = 512;
    static constexpr float SM_NearDist = 10.0f;
    static constexpr float SM_FarDist = 1000.0f;

	jDirectionalLight();
	virtual ~jDirectionalLight();

    void Initialize(const Vector& InDirection, const Vector& InColor, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower);

    virtual void Update(float deltaTime) override;
	virtual IUniformBufferBlock* GetUniformBufferBlock() const override { return LightDataUniformBlock; }
    virtual const jCamera* GetLightCamra(int32 index = 0) const override;

    virtual jShaderBindingInstance* PrepareShaderBindingInstance(jTexture* InShadowMap) const override
	{
		int32 BindingPoint = 0;
		jShaderBindingArray ShaderBindingArray;
		jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

		ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::UNIFORMBUFFER, EShaderAccessStageFlag::ALL_GRAPHICS
			, ResourceInlineAllocator.Alloc<jUniformBufferResource>(LightDataUniformBlock));

        if (ensure(InShadowMap))
        {
            const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllocator.Alloc<jTextureResource>(InShadowMap, ShadowSamplerStateInfo));
        }

        return g_rhi->CreateShaderBindingInstance(ShaderBindingArray);
    }

    FORCEINLINE const jLightDataUniformBuffer& GetLightData() const { return LightData; }

private:
    jCamera* Camera = nullptr;
    jLightDataUniformBuffer LightData;
    IUniformBufferBlock* LightDataUniformBlock = nullptr;
};
