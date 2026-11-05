#include "common.hlsl"

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    //[[vk::location(1)]] min16uint LayerIndex : TEXCOORD0;
    [[vk::location(1)]] uint LayerIndex : BLENDINDICES0;
};

#include "Features/materialvertex.hlsl"

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 WorldPos : TEXCOORD0;
    float Opacity : TEXCOORD1;
    //min16uint LayerIndex : SV_RenderTargetArrayIndex;
    uint LayerIndex : SV_RenderTargetArrayIndex;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    float3 waterNormal = float3(0, 1, 0), waterTangent = float3(1, 0, 0);
    JE_DeformWater(input.Position, waterNormal, waterTangent);

    output.LayerIndex = input.LayerIndex;
    output.Opacity = RenderObjectParam.BaseColorOpacity.a;
    output.WorldPos = mul(RenderObjectParam.M, float4(input.Position, 1.0));
    output.Pos = mul(PointLight.ShadowVP[input.LayerIndex], output.WorldPos);
    return output;
}
