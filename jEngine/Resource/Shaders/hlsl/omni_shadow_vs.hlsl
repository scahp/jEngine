#include "common.hlsl"

cbuffer PointLight : register(b0, space0) { jPointLightUniformBufferData PointLight; }
cbuffer RenderObjectParam : register(b0,space1) { RenderObjectUniformBuffer RenderObjectParam; }

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] min16uint LayerIndex : TEXCOORD0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 WorldPos : TEXCOORD0;
    min16uint LayerIndex : SV_RenderTargetArrayIndex;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    output.LayerIndex = input.LayerIndex;
    output.WorldPos = mul(RenderObjectParam.M, float4(input.Position, 1.0));
    output.Pos = mul(PointLight.ShadowVP[input.LayerIndex], output.WorldPos);
    return output;
}
