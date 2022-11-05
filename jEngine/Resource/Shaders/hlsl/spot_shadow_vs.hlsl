struct jSpotLightUniformBufferData
{
    float3 Position;
    float MaxDistance;

    float3 Direction;
    float PenumbraRadian;

    float3 Color;
    float UmbraRadian;

    float3 DiffuseIntensity;
    float SpecularPow;

    float3 SpecularIntensity;
    float padding0;

    float4x4 ShadowVP;
};

struct RenderObjectUniformBuffer
{
    float4x4 M;
    float4x4 InvM;
};

cbuffer SpotLight : register(b0, space0) { jSpotLightUniformBufferData SpotLight; }
cbuffer RenderObjectParam : register(b0,space1) { RenderObjectUniformBuffer RenderObjectParam; }

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.Pos = mul(SpotLight.ShadowVP, mul(RenderObjectParam.M, float4(input.Position, 1.0)));
    return output;
}
