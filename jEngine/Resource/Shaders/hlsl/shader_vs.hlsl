#include "common.hlsl"

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
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL0;
    float4 DirectionalLightShadowPosition : TEXCOORD1;
    float4 SpotLightShadowPosition : TEXCOORD2;
    float4 WorldPos : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    
    output.WorldPos = mul(RenderObjectParam.M, float4(input.Position, 1.0));
    output.Pos = mul(ViewParam.VP, output.WorldPos);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;

    output.Normal = normalize(mul((float3x3)RenderObjectParam.M, input.Normal));

    output.DirectionalLightShadowPosition = mul(DirectionalLight.ShadowVP, output.WorldPos);
    output.SpotLightShadowPosition = mul(SpotLight.ShadowVP, output.WorldPos);
    return output;
}
