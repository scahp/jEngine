#include "common.hlsl"

#ifndef USE_VERTEX_COLOR
#define USE_VERTEX_COLOR 0
#endif

#ifndef USE_VERTEX_BITANGENT
#define USE_VERTEX_BITANGENT 0
#endif

struct PushConsts
{
    float4x4 MVP;
};
//[[vk::push_constant]] PushConsts pushConsts;

cbuffer PushConsts : register(b0, space3) { PushConsts pushConsts; }

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
#if USE_VERTEX_COLOR
    [[vk::location((1))]] float4 Color : COLOR0;
#endif
    [[vk::location(1+USE_VERTEX_COLOR)]] float3 Normal : NORMAL0;
    [[vk::location(2+USE_VERTEX_COLOR)]] float3 Tangent : TANGENT0;
    [[vk::location(3+USE_VERTEX_COLOR)]] float3 Bitangent : BITANGENT0;
    [[vk::location(4+USE_VERTEX_COLOR)]] float2 TexCoord : TEXCOORD0;
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
