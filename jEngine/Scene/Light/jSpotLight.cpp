#include "pch.h"
#include "jSpotLight.h"

jSpotLight::jSpotLight()
    : jLight(ELightType::SPOT)
{
    LightDataUniformBlock = g_rhi->CreateUniformBufferBlock(jNameStatic("SpotLightBlock"), jLifeTimeType::MultiFrame);

    char szTemp[128] = { 0, };
    for (int i = 0; i < _countof(OmniShadowMapVP); ++i)
    {
        sprintf_s(szTemp, sizeof(szTemp), "OmniShadowMapVP[%d]", i);
        OmniShadowMapVP[i].Name = jName(szTemp);
    }
}

jSpotLight::~jSpotLight()
{
    delete LightDataUniformBlock;
    
    for (int32 i = 0; i < _countof(Camera); ++i)
    {
        delete Camera[i];
    }
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

    Camera[0] = jCamera::CreateCamera(InPos, InPos + Vector(1.0f, 0.0f, 0.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[1] = jCamera::CreateCamera(InPos, InPos + Vector(-1.0f, 0.0f, 0.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[2] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 1.0f, 0.0f), InPos + Vector(0.0f, 0.0f, -1.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[3] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, -1.0f, 0.0f), InPos + Vector(0.0f, 0.0f, 1.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[4] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, 1.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
    Camera[5] = jCamera::CreateCamera(InPos, InPos + Vector(0.0f, 0.0f, -1.0f), InPos + Vector(0.0f, 1.0f, 0.0f), DegreeToRadian(90.0f), SM_NearDist, SM_FarDist, SM_Width, SM_Height, true);
}

jCamera* jSpotLight::GetLightCamra(int index /*= 0*/) const
{
    check(index < _countof(Camera));
    return Camera[index];
}

jShaderBindingInstance* jSpotLight::PrepareShaderBindingInstance(jTexture* InShadowMap) const
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

void jSpotLight::Update(float deltaTime)
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

//void jSpotLight::SetupUniformBuffer()
//{
//    LightDataUniformBlock->UpdateBufferData(&Data, sizeof(Data));
//    if (ShadowMapData && ShadowMapData->IsValid())
//    {
//        // Both near and far are all the same within camera array.
//        struct ShadowData
//        {
//            float Near;
//            float Far;
//            Vector2 ShadowMapSize;
//            // 16 byte aligned don't need a padding
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
//
//        for (int i = 0; i < 6; ++i)
//        {
//            auto camera = ShadowMapData->ShadowMapCamera[i];
//            const auto vp = (camera->Projection * camera->View);
//            OmniShadowMapVP[i].Data = vp;
//        }
//    }
//}

