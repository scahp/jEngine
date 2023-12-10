#include "pch.h"
#include "jDirectionalLight.h"

jDirectionalLight::jDirectionalLight() : jLight(ELightType::DIRECTIONAL)
{
}

jDirectionalLight::~jDirectionalLight()
{
    delete Camera;
    LightDataUniformBlockPtr.reset();
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

    // 임시 코드, 쉐도우맵 해상도 대비 절반 크기를 카메라의 렌더링 영역으로 잡음.
    // 이 항목은 외부에서 설정 가능하게 뺄 예정
    const float CameraWidth = SM_Width;
    const float CameraHeight = SM_Height;

    Camera = jOrthographicCamera::CreateCamera(pos, target, up, -CameraWidth, -CameraHeight, CameraWidth, CameraHeight, SM_NearDist, SM_FarDist);
}

const jCamera* jDirectionalLight::GetLightCamra(int32 index) const
{
    return Camera;
}

const std::shared_ptr<jShaderBindingInstance>& jDirectionalLight::PrepareShaderBindingInstance(jTexture* InShadowMap)
{
    const auto CurrentFrameIndex = g_rhi->GetCurrentFrameIndex();
    if (NeedToUpdateShaderBindingInstance)
    {
        NeedToUpdateShaderBindingInstance = false;

        if (LightDataUniformBlockPtr)
            LightDataUniformBlockPtr->Free();

        LightDataUniformBlockPtr = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("DirectionalLightBlock"), jLifeTimeType::MultiFrame, sizeof(jDirectionalLightUniformBufferData)));
        LightDataUniformBlockPtr->UpdateBufferData(&LightData, sizeof(LightData));

        int32 BindingPoint = 0;
        jShaderBindingArray ShaderBindingArray;
        jShaderBindingResourceInlineAllocator ResourceInlineAllocator;

        ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::UNIFORMBUFFER_DYNAMIC, EShaderAccessStageFlag::ALL_GRAPHICS
            , ResourceInlineAllocator.Alloc<jUniformBufferResource>(LightDataUniformBlockPtr.get()));

        // Create LightOnlyData (without ShadowMap, for rendering shadowmap)
        {
            if (ShaderBindingInstanceOnlyLightData)
                ShaderBindingInstanceOnlyLightData->Free();
            ShaderBindingInstanceOnlyLightData = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
        }

        // Create WithShadowMap (for rendering lighting passes)
        {
            const jSamplerStateInfo* ShadowSamplerStateInfo = TSamplerStateInfo<ETextureFilter::LINEAR, ETextureFilter::LINEAR
                , ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER, ETextureAddressMode::CLAMP_TO_BORDER
                , 0.0f, 1.0f, Vector4(1.0f, 1.0f, 1.0f, 1.0f), true, ECompareOp::LESS>::Create();

            ShaderBindingArray.Add(BindingPoint++, 1, EShaderBindingType::TEXTURE_SAMPLER_SRV, EShaderAccessStageFlag::ALL_GRAPHICS
                , ResourceInlineAllocator.Alloc<jTextureResource>(InShadowMap, ShadowSamplerStateInfo));

            if (ShaderBindingInstanceWithShadowMap)
                ShaderBindingInstanceWithShadowMap->Free();

            ShaderBindingInstanceWithShadowMap = g_rhi->CreateShaderBindingInstance(ShaderBindingArray, jShaderBindingInstanceType::MultiFrame);
        }
    }

    return InShadowMap ? ShaderBindingInstanceWithShadowMap : ShaderBindingInstanceOnlyLightData;
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
        NeedToUpdateShaderBindingInstance = true;
    }
    
    if (LightData.ShadowV != Camera->View)
    {
        LightData.ShadowV = Camera->View;
        NeedToUpdateShaderBindingInstance = true;
    }
}

