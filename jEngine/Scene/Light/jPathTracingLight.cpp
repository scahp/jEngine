#include "pch.h"
#include "jPathTracingLight.h"
#include "Shader/jShaderParameterSet.h"

template <>
struct TShaderParameterHLSLTypeInfo<jPathTracingLightUniformBufferData>
{
    static constexpr const char* GetTypeName() { return "jPathTracingLightUniformBufferData"; }
    static void AppendTypeDeclaration(std::string&) {}
};

BEGIN_SHADER_PARAMETER_SET(jPathTracingLightShaderParameters)
    SHADER_UNIFORM_BUFFER(jPathTracingLightUniformBufferData, PathTracingLight)
END_SHADER_PARAMETER_SET()

void jPathTracingLight::Initialize(const jPathTracingLightUniformBufferData& InData)
{
    LightData = InData;
}

const std::shared_ptr<jShaderBindingInstance>& jPathTracingLight::PrepareShaderBindingInstance(jTexture* InShadowMap)
{
    if (IsNeedToUpdateShaderBindingInstance)
    {
        IsNeedToUpdateShaderBindingInstance = false;

        LightDataUniformBufferPtr = std::shared_ptr<IUniformBufferBlock>(
            g_rhi->CreateUniformBufferBlock(jNameStatic("PathTracingLightBlock"), jLifeTimeType::MultiFrame, sizeof(jPathTracingLightUniformBufferData)));
        LightDataUniformBufferPtr->UpdateBufferData(&LightData, sizeof(LightData));

        if (ShaderBindingInstanceDataPtr)
            ShaderBindingInstanceDataPtr->Free();

        jPathTracingLightShaderParameters Parameters;
        Parameters.PathTracingLight.Buffer = LightDataUniformBufferPtr;
        ShaderBindingInstanceDataPtr = jShaderParameterSet::CreateShaderBindingInstance(
            Parameters, EShaderAccessStageFlag::ALL_GRAPHICS, jShaderBindingInstanceType::MultiFrame);
    }

    return ShaderBindingInstanceDataPtr;
}
