#include "common.hlsl"

struct VSInput
{
    [[vk::location((0))]] float3 Position : POSITION0;
};

cbuffer ViewParam : register(b0, space0) { ViewUniformBuffer ViewParam; }
cbuffer RenderObjectParam : register(b0, space1) { RenderObjectUniformBuffer RenderObjectParam; }

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;

    float4 worldPos = mul(RenderObjectParam.M, float4(input.Position, 1.0));
    output.Pos = mul(ViewParam.VP, worldPos);

    return output;
}
