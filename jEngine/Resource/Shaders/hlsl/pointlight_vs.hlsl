#include "common.hlsl"

struct PushConsts
{
    float4x4 MVP;
};
[[vk::push_constant]] PushConsts pushConsts;

cbuffer PointLight : register(b0,space1) { jPointLightUniformBufferData PointLight; }
TextureCube PointLightShadowCubeMap : register(t1, space1);
SamplerComparisonState PointLightShadowMapSampler : register(s1, space1);

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float4 Color : COLOR0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float3 Tangent : NORMAL1;
    [[vk::location(4)]] float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 ClipPos : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.Pos = mul(pushConsts.MVP, float4(input.Position, 1.0));
    output.ClipPos = output.Pos;
    return output;
}
