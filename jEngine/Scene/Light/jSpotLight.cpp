#include "pch.h"
#include "jSpotLight.h"

jSpotLight::jSpotLight()
    : jLight(ELightType::SPOT)
{
    LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("SpotLightBlock"), jLifeTimeType::MultiFrame, sizeof(jSpotLightUniformBufferData));
}

jSpotLight::~jSpotLight()
{
    delete LightDataUniformBlock;
    delete Camera;
}

void jSpotLight::Initialize(const Vector& InPos, const Vector& InDirection, const Vector& InColor, float InMaxDistance
    , float InPenumbraRadian, float InUmbraRadian, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower)
{
    LightData.Position = InPos;
    LightData.Direction = InDirection;
    LightData.Color = InColor;
    LightData.DiffuseIntensity = InDiffuseIntensity;
    LightData.SpecularIntensity = InSpecularIntensity;
    LightData.SpecularPow = InSpecularPower;
    LightData.MaxDistance = InMaxDistance;
    LightData.PenumbraRadian = InPenumbraRadian;
    LightData.UmbraRadian = InUmbraRadian;

    SM_FarDist = InMaxDistance;

    constexpr float FOV = PI / 2.0f;
    Camera = jCamera::CreateCamera(InPos, InPos + InDirection, InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(120.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
}

jCamera* jSpotLight::GetLightCamra(int index /*= 0*/) const
{
    return Camera;
}

jShaderBindingInstance* jSpotLight::PrepareShaderBindingInstance(jTexture* InShadowMap)
{
    if (LastUsedShadowMap != InShadowMap)
    {
        IsNeedToUpdateShaderBindingInstance = true;
    }

    if (IsNeedToUpdateShaderBindingInstance)
    {
        IsNeedToUpdateShaderBindingInstance = false;

        LightDataUniformBlock->UpdateBufferData(&LightData, sizeof(LightData));

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

        ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllocator.Alloc<jUniformBufferResource>(LightDataUniformBlock));

        if (InShadowMap)
        {
            const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), true, ECompareOp::LESS>::Create();

            ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllocator.Alloc<jTextureResource>(InShadowMap, ShadowSamplerStateInfo));
        }
        LastUsedShadowMap = InShadowMap;

        if (ShaderBindingInstance)
            ShaderBindingInstance->Free();

        ShaderBindingInstance = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
    }
    return ShaderBindingInstance;
}

const Matrix* jSpotLight::GetLightWorldMatrix() const
{
    return &LightWorldMatrix;
}

void jSpotLight::Update(float deltaTime)
{
    check(Camera);
    Camera->SetEulerAngle(Vector::GetEulerAngleFrom(LightData.Direction));
    Camera->UpdateCamera();

    // Prepare light data for uniform buffer
    const Matrix VP = Camera->Projection * Camera->View;
    if (LightData.ShadowVP != VP)
    {
        LightData.ShadowVP = VP;
        IsNeedToUpdateShaderBindingInstance = true;
    }

    // Prepare light world matrix for push constant
    LightWorldMatrix = Matrix::MakeTranlsateAndScale(LightData.Position, Vector(LightData.MaxDistance));
}
