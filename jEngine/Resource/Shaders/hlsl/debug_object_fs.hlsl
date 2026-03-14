#include "common.hlsl"

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    float4 DiffuseColor = DiffuseTexture.Sample(DiffuseTextureSampler, input.TexCoord.xy);
    return DiffuseColor;
}
