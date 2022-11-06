#include "pch.h"
#include "jPointLight.h"

jPointLight::jPointLight()
    : jLight(ELightType::POINT)
{
    LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("PointLightBlock"), jLifeTimeType::MultiFrame, sizeof(jPointLightUniformBufferData));
}

jPointLight::~jPointLight()
{
    delete LightDataUniformBlock;

    for (int32 i = 0; i < _countof(Camera); ++i)
    {
        delete Camera[i];
    }
}

void jPointLight::Initialize(const Vector& InPos, const Vector& InColor, float InMaxDist, const Vector& InDiffuseIntensity, const Vector& InSpecularIntensity, float InSpecularPower)
{
    LightData.Position = InPos;
    LightData.Color = InColor;
    LightData.DiffuseIntensity = InDiffuseIntensity;
    LightData.SpecularIntensity = InSpecularIntensity;
    LightData.SpecularPow = InSpecularPower;
    LightData.MaxDistance = InMaxDist;

    constexpr float FOV = PI / 2.0f;

    Camera[0] = jCamera::CreateCamera(InPos, InPos + Vector(1.0f, 0.0f, 0.0f),     InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[1] = jCamera::CreateCamera(InPos, InPos + Vector(-1.0f, 0.0f, 0.0f),    InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[2] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 1.0f, 0.0f),     InPos + Vector(0.0f, 0.0f, -1.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[3] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, -1.0f, 0.0f),    InPos + Vector(0.0f, 0.0f, 1.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[4] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, 1.0f),     InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[5] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, -1.0f),    InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
}

jShaderBindingInstance* jPointLight::PrepareShaderBindingInstance(jTexture* InShadowMap) const
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
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), true, ECompareOp::LESS>::Create();
        
        ShaderBindingArray.Add(BindingPoint++, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllocator.Alloc<jTextureResource>(InShadowMap, ShadowSamplerStateInfo));
    }

    return g_rhi->CreateShaderBindingInstance(ShaderBindingArray);
}


jCamera* jPointLight::GetLightCamra(int index /*= 0*/) const
{
    check(index < _countof(Camera));
    return Camera[index];
}

void jPointLight::Update(float deltaTime)
{
    for (int32 i = 0; i < _countof(Camera); ++i)
    {
        auto currentCamera = Camera[i];
        const auto offset = LightData.Position - currentCamera->Pos;
        currentCamera->Pos = LightData.Position;
        currentCamera->Target += offset;
        currentCamera->Up += offset;
        currentCamera->UpdateCamera();

        LightData.ShadowVP[i] = currentCamera->Projection * currentCamera->View;
    }

    LightDataUniformBlock->UpdateBufferData(&LightData, sizeof(LightData));
}
