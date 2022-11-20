#include "common.hlsl"

float WindowingFunction(float value, float maxValue)
{
    return pow(max(0.0, 1.0 - pow(value / maxValue, 4.0)), 2.0);
}

float DistanceAttenuation(float distance, float maxDistance)
{
    const float refDistance = 50.0;
    float attenuation = (refDistance * refDistance) / ((distance * distance) + 1.0);
    return attenuation * WindowingFunction(distance, maxDistance);
}

float Square(float x)
{
    return x * x;
}

float DiretionalFalloff(float lightRadian, float penumbraRadian, float umbraRadian)
{
    float t = clamp((cos(lightRadian) - cos(umbraRadian)) / (cos(penumbraRadian) - cos(umbraRadian)), 0.0, 1.0);
    return t * t;
}

float3 GetDirectionalLightDiffuse(jDirectionalLightUniformBuffer light, float3 normal)
{
    return light.Color * clamp(dot(-light.Direction, normal), 0.0, 1.0) * light.DiffuseIntensity;
}

float3 GetDirectionalLightSpecular(jDirectionalLightUniformBuffer light, float3 reflectLightDir, float3 viewDir)
{
    return light.Color * pow(clamp(dot(reflectLightDir, viewDir), 0.0, 1.0), light.SpecularPow) * light.SpecularIntensity;
}

float3 GetDirectionalLight(jDirectionalLightUniformBuffer light, float3 normal, float3 viewDir)
{
    float3 lightDir = normalize(-light.Direction);
    float3 reflectLightDir = 2.0 * clamp(dot(lightDir, normal), 0.0, 1.0) * normal - lightDir;
    return (GetDirectionalLightDiffuse(light, normal) + GetDirectionalLightSpecular(light, reflectLightDir, viewDir));
}

float DistanceAttenuation2(float DistanceSqr, float InvMaxDistance)
{
    return Square(saturate(1 - Square(DistanceSqr * Square(InvMaxDistance))));
}

float3 GetPointLightDiffuse(jPointLightUniformBufferData light, float3 normal, float3 lightDir)
{
    return light.Color * clamp(dot(lightDir, normal), 0.0, 1.0) * light.DiffuseIntensity;
}

float3 GetPointLightSpecular(jPointLightUniformBufferData light, float3 reflectLightDir, float3 viewDir)
{
    return light.Color * pow(clamp(dot(reflectLightDir, viewDir), 0.0, 1.0), light.SpecularPow) * light.SpecularIntensity;
}

float3 GetPointLight(jPointLightUniformBufferData light, float3 normal, float3 pixelPos, float3 viewDir)
{
    float3 lightDir = light.Position - pixelPos;
    float distanceSqr = dot(lightDir, lightDir);
    lightDir = normalize(lightDir);
    float3 reflectLightDir = 2.0 * clamp(dot(lightDir, normal), 0.0, 1.0) * normal - lightDir;

    return (GetPointLightDiffuse(light, normal, lightDir) + GetPointLightSpecular(light, reflectLightDir, viewDir)) * DistanceAttenuation2(distanceSqr, 1.0f / light.MaxDistance);
}

float3 GetSpotLightDiffuse(jSpotLightUniformBufferData light, float3 normal, float3 lightDir)
{
    return light.Color * clamp(dot(lightDir, normal), 0.0, 1.0) * light.DiffuseIntensity;
}

float3 GetSpotLightSpecular(jSpotLightUniformBufferData light, float3 reflectLightDir, float3 viewDir)
{
    return light.Color * pow(clamp(dot(reflectLightDir, viewDir), 0.0, 1.0), light.SpecularPow) * light.SpecularIntensity;
}

float3 GetSpotLight(jSpotLightUniformBufferData light, float3 normal, float3 pixelPos, float3 viewDir)
{
    float3 lightDir = light.Position - pixelPos;
    float distanceSqr = dot(lightDir, lightDir);
    lightDir = normalize(lightDir);
    float3 reflectLightDir = 2.0 * clamp(dot(lightDir, normal), 0.0, 1.0) * normal - lightDir;

    float lightRadian = acos(dot(lightDir, -light.Direction));

    return (GetSpotLightDiffuse(light, normal, lightDir)
        + GetSpotLightSpecular(light, reflectLightDir, viewDir))
        * DistanceAttenuation2(distanceSqr, 1.0f / light.MaxDistance)
        * DiretionalFalloff(lightRadian, light.PenumbraRadian, light.UmbraRadian);
}
