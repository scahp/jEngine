#include "pch.h"
#include "jPointLight.h"

jPointLight::jPointLight()
    : jLight(ELightType::POINT)
{
    LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("PointLightBlock"), jLifeTimeType::MultiFrame);

    char szTemp[128] = { 0, };
    for (int i = 0; i < 6; ++i)
    {
        sprintf_s(szTemp, sizeof(szTemp), "OmniShadowMapVP[%d]", i);
        OmniShadowMapVP[i].Name = jName(szTemp);
    }
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

    Camera[0] = jCamera::CreateCamera(InPos, InPos + Vector(1.0f, 0.0f, 0.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[1] = jCamera::CreateCamera(InPos, InPos + Vector(-1.0f, 0.0f, 0.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[2] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 1.0f, 0.0f), InPos + Vector(0.0f, 0.0f, -1.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[3] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, -1.0f, 0.0f), InPos + Vector(0.0f, 0.0f, 1.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[4] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, 1.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[5] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, -1.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);

}

//void jPointLight::BindLight(const jShader* shader) const
//{
//	LightDataUniformBlock->Bind(shader);
//
//	if (ShadowMapData && ShadowMapData->IsValid())
//	{
//		const IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
//		shadowDataUniformBlock->Bind(shader);
//	}
//
//	for (int i = 0; i < 6; ++i)
//	{
//		OmniShadowMapVP[i].SetUniformbuffer(shader);
//	}
//}

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
    }

    check(0); // todo
    //SetupUniformBuffer();
    return;

    //for (int32 i = 0; i < 6; ++i)
    //{
    //    auto currentCamera = Camera[i];
    //    const auto vp = (currentCamera->Projection * currentCamera->View);
    //    OmniShadowMapVP[i].Data = vp;
    //}
}

//void jPointLight::SetupUniformBuffer()
//{
//    LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
//
//    if (ShadowMapData && ShadowMapData->IsValid())
//    {
//        struct ShadowData
//        {
//            float Near;
//            float Far;
//            Vector2 ShadowMapSize;
//
//            bool operator == (const ShadowData& rhs) const
//            {
//                return (Near == rhs.Near) && (Far == rhs.Far) && (ShadowMapSize == rhs.ShadowMapSize);
//            }
//
//            bool operator != (const ShadowData& rhs) const
//            {
//                return !(*this == rhs);
//            }
//
//            void SetData(jLightUtil::jShadowMapArrayData* shadowMapData)
//            {
//                auto camera = shadowMapData->ShadowMapCamera[0];
//                JASSERT(camera);
//
//				// todo
//				check(0);
//                //const auto& renderTargetInfo = shadowMapData->ShadowMapFrameBuffer->Info;
//
//                //Near = camera->Near;
//                //Far = camera->Far;
//                //ShadowMapSize.x = static_cast<float>(renderTargetInfo.Width);
//                //ShadowMapSize.y = static_cast<float>(renderTargetInfo.Height);
//            }
//        };
//
//        ShadowData shadowData;
//        shadowData.SetData(ShadowMapData);
//
//        IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
//        shadowDataUniformBlock->UpdateBufferData(&shadowData, sizeof(shadowData));
//    }
//}

//void jSpotLight::BindLight(const jShader* shader) const
//{
//	LightDataUniformBlock->Bind(shader);
//
//	if (ShadowMapData && ShadowMapData->IsValid())
//	{
//		const IUniformBufferBlock* shadowDataUniformBlock = ShadowMapData->UniformBlock;
//		shadowDataUniformBlock->Bind(shader);
//	}
//
//	for (int i = 0; i < 6; ++i)
//	{
//		OmniShadowMapVP[i].SetUniformbuffer(shader);
//	}
//}