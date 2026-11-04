#include "common.hlsl"

struct GSOutput
{
    float4 Pos : SV_POSITION;
    float4 WorldPos : TEXCOORD0;
    float Opacity : TEXCOORD1;
    //min16uint LayerIndex : SV_RenderTargetArrayIndex;
    uint LayerIndex : SV_RenderTargetArrayIndex;
};

void main(GSOutput input, out float OutDepth : SV_Depth) : SV_TARGET
{
    if (input.Opacity <= frac(dot(input.Pos.xy, float2(0.754877666, 0.569840296)))) discard;
    float lightDistance = length(input.WorldPos.xyz - PointLight.Position);
    OutDepth = lightDistance / PointLight.MaxDistance;
}
