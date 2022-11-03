#include "pch.h"
#include "jPointLight.h"

jPointLight::jPointLight()
    : jLight(ELightType::POINT)
{
    LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("PointLightBlock"), jLifeTimeType::MultiFrame, sizeof(jPointLightUniformBufferData));

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

    constexpr float FOV = PI / 2.0f;

    Camera[0] = jCamera::CreateCamera(InPos, InPos + Vector(1.0f, 0.0f, 0.0f),     InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[1] = jCamera::CreateCamera(InPos, InPos + Vector(-1.0f, 0.0f, 0.0f),    InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[2] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 1.0f, 0.0f),     InPos + Vector(0.0f, 0.0f, -1.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[3] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, -1.0f, 0.0f),    InPos + Vector(0.0f, 0.0f, 1.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[4] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, 1.0f),     InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[5] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, -1.0f),    InPos + Vector(0.0f, 1.0f, 0.0f), FOV, SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
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
            , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f)>::Create();

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