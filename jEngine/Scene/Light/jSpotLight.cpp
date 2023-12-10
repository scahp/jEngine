#include "pch.h"
#include "jSpotLight.h"

jSpotLight::jSpotLight()
    : jLight(ELightType::SPOT)
{
}

jSpotLight::~jSpotLight()
{
    delete Camera;
    LightDataUniformBlockPtr.reset();
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

const std::shared_ptr<jShaderBindingInstance>& jSpotLight::PrepareShaderBindingInstance(jTexture* InShadowMap)
{
    if (IsNeedToUpdateShaderBindingInstance)
    {
        IsNeedToUpdateShaderBindingInstance = false;

        if (LightDataUniformBlockPtr)
            LightDataUniformBlockPtr->Free();

        LightDataUniformBlockPtr = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("SpotLightBlock"), jLifeTimeType::MultiFrame, sizeof(jSpotLightUniformBufferData)));
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
	    #if USE_REVERSEZ_PERSPECTIVE_SHADOW
	    LightData.ShadowVP = Camera->ReverseZProjection * Camera->View;
	    #else
	    LightData.ShadowVP = Camera->Projection * Camera->View;
	    #endif
        IsNeedToUpdateShaderBindingInstance = true;
    }

    // Prepare light world matrix for push constant
    LightWorldMatrix = Matrix::MakeTranlsateAndScale(LightData.Position, Vector(LightData.MaxDistance));
}
