#pragma once

#include "Shader/jCommonShaderParameters.h"
#include "Scene/Light/jDirectionalLight.h"
#include "Scene/Light/jPointLight.h"
#include "Scene/Light/jSpotLight.h"

template <>
struct TShaderParameterHLSLTypeInfo<jDirectionalLightUniformBufferData>
{
    static constexpr const char* GetTypeName() { return "jDirectionalLightUniformBuffer"; }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<jPointLightUniformBufferData>
{
    static constexpr const char* GetTypeName() { return "jPointLightUniformBufferData"; }
    static void AppendTypeDeclaration(std::string&) {}
};

template <>
struct TShaderParameterHLSLTypeInfo<jSpotLightUniformBufferData>
{
    static constexpr const char* GetTypeName() { return "jSpotLightUniformBufferData"; }
    static void AppendTypeDeclaration(std::string&) {}
};

BEGIN_SHADER_UNIFORM_BUFFER_STRUCT(jLightVolumeVertexUniformBuffer)
    SHADER_UNIFORM_BUFFER_MEMBER(Matrix, MVP)
END_SHADER_UNIFORM_BUFFER_STRUCT()

BEGIN_SHADER_PARAMETER_SET(jSceneTexturesShaderParameters)
    SHADER_TEXTURE2D(GBuffer0)
    SHADER_TEXTURE2D(GBuffer1)
    SHADER_TEXTURE2D(GBuffer2)
    SHADER_TEXTURE2D(DepthTexture)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSceneSubpassInputShaderParameters)
    SHADER_SUBPASS_INPUT(GBuffer0)
    SHADER_SUBPASS_INPUT(GBuffer1)
    SHADER_SUBPASS_INPUT(GBuffer2)
    SHADER_SUBPASS_INPUT_FLOAT(DepthTexture)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jDirectionalLightShaderParameters)
    SHADER_UNIFORM_BUFFER(jDirectionalLightUniformBufferData, DirectionalLight)
    SHADER_TEXTURE2D_COMPARISON(DirectionalLightShadowMap)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jDirectionalLightOnlyShaderParameters)
    SHADER_UNIFORM_BUFFER(jDirectionalLightUniformBufferData, DirectionalLight)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jPointLightShaderParameters)
    SHADER_UNIFORM_BUFFER(jPointLightUniformBufferData, PointLight)
    SHADER_TEXTURECUBE_COMPARISON(PointLightShadowCubeMap)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jPointLightOnlyShaderParameters)
    SHADER_UNIFORM_BUFFER(jPointLightUniformBufferData, PointLight)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSpotLightShaderParameters)
    SHADER_UNIFORM_BUFFER(jSpotLightUniformBufferData, SpotLight)
    SHADER_TEXTURE2D_COMPARISON(SpotLightShadowMap)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jSpotLightOnlyShaderParameters)
    SHADER_UNIFORM_BUFFER(jSpotLightUniformBufferData, SpotLight)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jDirectionalLightIBLShaderParameters)
    SHADER_TEXTURECUBE(IrradianceMap)
    SHADER_TEXTURECUBE(PrefilteredEnvMap)
END_SHADER_PARAMETER_SET()

BEGIN_SHADER_PARAMETER_SET(jLightVolumeVertexShaderParameters)
    SHADER_UNIFORM_BUFFER(jLightVolumeVertexUniformBuffer, PushConsts)
END_SHADER_PARAMETER_SET()
