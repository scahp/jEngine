#include "pch.h"
#include "jPointLight.h"
#include "Shader/jLightingShaderParameters.h"

jPointLight::jPointLight()
    : jLight(ELightType::POINT)
{
}

jPointLight::~jPointLight()
{
    for (int32 i = 0; i < _countof(Camera); ++i)
    {
        delete Camera[i];
    }

    LightDataUniformBlockPtr.reset();
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

const std::shared_ptr<jShaderBindingInstance>& jPointLight::PrepareShaderBindingInstance(jTexture* InShadowMap)
{
    if (IsNeedToUpdateShaderBindingInstance)
    {
        IsNeedToUpdateShaderBindingInstance = false;

        LightDataUniformBlockPtr = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("PointLightBlock"), jLifeTimeType::MultiFrame, sizeof(jPointLightUniformBufferData)));
        LightDataUniformBlockPtr->UpdateBufferData(&LightData, sizeof(LightData));

        // Create LightOnlyData (without ShadowMap, for rendering shadowmap)
        {
            if (ShaderBindingInstanceOnlyLightData)
                ShaderBindingInstanceOnlyLightData->Free();
            jPointLightOnlyShaderParameters Parameters;
            Parameters.PointLight.Buffer = LightDataUniformBlockPtr;
            ShaderBindingInstanceOnlyLightData = jShaderParameterSet::CreateShaderBindingInstance(Parameters, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::MultiFrame);
        }

        // Create WithShadowMap (for rendering lighting passes)
        {
            const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), true, ECompareOp::LESS>::Create();

            if (ShaderBindingInstanceWithShadowMap)
                ShaderBindingInstanceWithShadowMap->Free();
            jPointLightShaderParameters Parameters;
            Parameters.PointLight.Buffer = LightDataUniformBlockPtr;
            Parameters.PointLightShadowCubeMap = { InShadowMap ? InShadowMap : GWhiteCubeTexture.get(), ShadowSamplerStateInfo };
            ShaderBindingInstanceWithShadowMap = jShaderParameterSet::CreateShaderBindingInstance(Parameters, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::MultiFrame);
        }
    }
    return InShadowMap ? ShaderBindingInstanceWithShadowMap : ShaderBindingInstanceOnlyLightData;
}

jCamera* jPointLight::GetLightCamra(int index /*= 0*/) const
{
    check(index < _countof(Camera));
    return Camera[index];
}

const Matrix* jPointLight::GetLightWorldMatrix() const 
{
    return &LightWorldMatrix;
}

void jPointLight::Update(float deltaTime)
{
    __super::Update(deltaTime);

    // Prepare light data for uniform buffer
    for (int32 i = 0; i < _countof(Camera); ++i)
    {
        auto currentCamera = Camera[i];
        const auto offset = LightData.Position - currentCamera->Pos;
        currentCamera->Pos = LightData.Position;
        currentCamera->Target += offset;
        currentCamera->Up += offset;
        currentCamera->UpdateCamera();

        const Matrix VP = currentCamera->Projection * currentCamera->View;
        if (LightData.ShadowVP[i] != VP)
        {
            LightData.ShadowVP[i] = VP;
            IsNeedToUpdateShaderBindingInstance = true;
        }
    }

    // Prepare light world matrix for push constant
    LightWorldMatrix = Matrix::MakeTranlsateAndScale(LightData.Position, Vector(LightData.MaxDistance));
}
