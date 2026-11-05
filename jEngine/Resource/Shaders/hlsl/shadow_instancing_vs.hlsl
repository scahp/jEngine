#include "common.hlsl"

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    
    // instancing data
    [[vk::location(5)]] float4 InstancingColor : TEXCOORD1;
    [[vk::location(6)]] float3 InstancingWorld : TEXCOORD2;
};

#include "Features/materialvertex.hlsl"

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float Opacity : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;

    float3 waterNormal = float3(0, 1, 0), waterTangent = float3(1, 0, 0);
    JE_DeformWater(input.Position, waterNormal, waterTangent);
    output.Pos = mul(DirectionalLight.ShadowVP, mul(RenderObjectParam.M, float4((input.Position + input.InstancingWorld), 1.0)));
    output.Opacity = RenderObjectParam.BaseColorOpacity.a;
    return output;
}
