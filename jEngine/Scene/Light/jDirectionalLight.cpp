#include "pch.h"
#include "jDirectionalLight.h"

jDirectionalLight::jDirectionalLight() : jLight(ELightType::DIRECTIONAL)
{
    LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("DirectionalLightBlock"), jLifeTimeType::MultiFrame, sizeof(jLightDataUniformBuffer));
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

void jDirectionalLight::Update(float deltaTime)
{
    check(Camera);
    Camera->UpdateCamera();

    // Need dirty check
    jLightUtil::MakeDirectionalLightViewInfo(Camera->Pos, Camera->Target, Camera->Up, LightData.Direction);
    LightData.ShadowVP = (Camera->Projection * Camera->View);
    LightData.ShadowV = Camera->View;

    LightDataUniformBlock->UpdateBufferData(&LightData, sizeof(LightData));
}

