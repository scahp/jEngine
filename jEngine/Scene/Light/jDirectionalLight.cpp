#include "pch.h"
#include "jDirectionalLight.h"

jDirectionalLight::jDirectionalLight() : jLight(ELightType::DIRECTIONAL)
{
    LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("DirectionalLightBlock"), jLifeTimeType::MultiFrame, sizeof(jDirectionalLightUniformBufferData));
}

jDirectionalLight::~jDirectionalLight()
{
    delete Camera;
    delete LightDataUniformBlock;
}

void jDirectionalLight::Initialize(const Vector& InDirection, const Vector& InColor, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower)
{
    LightData.Direction = InDirection;
    LightData.Color = InColor;
    LightData.DiffuseIntensity = InDiffuseIntensity;
    LightData.SpecularIntensity = InSpecularIntensity;
    LightData.SpecularPow = InSpecularPower;

    Vector pos, target, up;
    pos = Vector(350.0f, 360.0f, 100.0f);
    jLightUtil::MakeDirectionalLightViewInfo(pos, target, up, InDirection);

    Camera = jOrthographicCamera::CreateCamera(pos, target, up, -SM_Width / 2.0f, -SM_Height / 2.0f, SM_Width / 2.0f, SM_Height / 2.0f, SM_NearDist, SM_FarDist);
}

const jCamera* jDirectionalLight::GetLightCamra(int32 index) const
{
    return Camera;
}

jShaderBindingInstance* jDirectionalLight::PrepareShaderBindingInstance(jTexture* InShadowMap)
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

void jDirectionalLight::Update(float deltaTime)
{
    check(Camera);
    Camera->SetEulerAngle(Vector::GetEulerAngleFrom(LightData.Direction));
    Camera->UpdateCamera();

    // Need dirty check
    jLightUtil::MakeDirectionalLightViewInfo(Camera->Pos, Camera->Target, Camera->Up, LightData.Direction);
    const Matrix VP = (Camera->Projection * Camera->View);
    if (LightData.ShadowVP != VP)
    {
        LightData.ShadowVP = VP;
        IsNeedToUpdateShaderBindingInstance = true;
    }
    
    if (LightData.ShadowV != Camera->View)
    {
        LightData.ShadowV = Camera->View;
        IsNeedToUpdateShaderBindingInstance = true;
    }
}

