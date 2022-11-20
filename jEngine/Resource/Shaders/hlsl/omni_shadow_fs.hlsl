#include "common.hlsl"

struct GSOutput
{
    float4 Pos : SV_POSITION;
    float4 WorldPos : TEXCOORD0;
    int Layer : SV_RenderTargetArrayIndex;
};

cbuffer PointLight : register(b0, space0) { jPointLightUniformBufferData PointLight; }

void main(GSOutput input, out float OutDepth : SV_Depth) : SV_TARGET
{
    float lightDistance = length(input.WorldPos.xyz - PointLight.Position);
    OutDepth = lightDistance / PointLight.MaxDistance;
}
