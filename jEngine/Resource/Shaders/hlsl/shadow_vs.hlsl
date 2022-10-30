struct DirectionalLightUniformBuffer
{
    float3 Direction;
    float SpecularPow;

    float3 Color;
    float padding0;

    float3 DiffuseIntensity;
    float padding1;

    float3 SpecularIntensity;
    float padding2;

    float4x4 ShadowVP;
    float4x4 ShadowV;
		
    float3 LightPos;
    float padding3;

    float2 ShadowMapSize;
    float Near;
    float Far;
};

struct RenderObjectUniformBuffer
{
    float4x4 M;
    float4x4 InvM;
};

cbuffer DirectionalLight : register(b0,space0) { DirectionalLightUniformBuffer DirectionalLight; }
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
    output.Pos = mul(DirectionalLight.ShadowVP, mul(RenderObjectParam.M, float4(input.Position, 1.0)));
    return output;
}
