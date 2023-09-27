#include "common.hlsl"

cbuffer DirectionalLight : register(b0,space0) { jDirectionalLightUniformBuffer DirectionalLight; }
cbuffer RenderObjectParam : register(b1,space0) { RenderObjectUniformBuffer RenderObjectParam; }

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    
    // instancing data
    [[vk::location(5)]] float4 InstancingColor : TEXCOORD1;
    [[vk::location(6)]] float3 InstancingWorld : TEXCOORD2;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    output.Pos = mul(DirectionalLight.ShadowVP, mul(RenderObjectParam.M, float4((input.Position + input.InstancingWorld), 1.0)));
    return output;
}
