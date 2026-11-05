#include "common.hlsl"

#ifndef USE_VERTEX_COLOR
#define USE_VERTEX_COLOR 1
#endif
#ifndef USE_VERTEX_BITANGENT
#define USE_VERTEX_BITANGENT 0
#endif

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
#if USE_VERTEX_COLOR
    [[vk::location(1)]] float4 Color : COLOR0;
#endif
    [[vk::location((1+USE_VERTEX_COLOR))]] float3 Normal : NORMAL0;
    [[vk::location((2+USE_VERTEX_COLOR))]] float3 Tangent : TANGENT0;
#if USE_VERTEX_BITANGENT
    [[vk::location((3+USE_VERTEX_COLOR))]] float3 Bitangent : BITANGENT0;
#endif
    [[vk::location((3+USE_VERTEX_COLOR+USE_VERTEX_BITANGENT))]] float2 TexCoord : TEXCOORD0;
    
    // instancing data
    [[vk::location(5)]] float4 InstancingColor : TEXCOORD1;
    [[vk::location(6)]] float3 InstancingWorld : TEXCOORD2;
};

#include "Features/materialvertex.hlsl"

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL0;
    float4 DirectionalLightShadowPosition : TEXCOORD1;
    float4 SpotLightShadowPosition : TEXCOORD2;
    float4 WorldPos : TEXCOORD3;
    float3x3 TBN : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    
    JE_DeformWater(input.Position, input.Normal, input.Tangent);
    float3 pos = input.Position + input.InstancingWorld;
    
    output.WorldPos = mul(RenderObjectParam.M, float4(pos, 1.0));
    output.Pos = mul(ViewParam.VP, output.WorldPos);
    output.Color = input.InstancingColor;
    //output.Color = input.Color;
    output.TexCoord = input.TexCoord;

    output.Normal = normalize(mul(input.Normal, (float3x3)RenderObjectParam.InvM));
    float3 tangent = mul((float3x3)RenderObjectParam.M, input.Tangent);
    tangent -= output.Normal * dot(tangent, output.Normal);
    if (dot(tangent, tangent) < 1.0e-12)
        tangent = cross(abs(output.Normal.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), output.Normal);
    tangent = normalize(tangent);
#if USE_VERTEX_BITANGENT
    float3 localBitangent = input.Bitangent;
#else
    float3 localBitangent = cross(input.Normal, input.Tangent);
#endif
    if (MaterialGraphParam.WaterControl.x > 0.5) localBitangent = -cross(input.Normal, input.Tangent);
    const float3 transformedBitangent = mul((float3x3)RenderObjectParam.M, localBitangent);
    const float handedness = dot(cross(output.Normal, tangent), transformedBitangent) < 0.0 ? -1.0 : 1.0;
    output.TBN = transpose(float3x3(tangent, normalize(cross(output.Normal, tangent)) * handedness, output.Normal));
    output.DirectionalLightShadowPosition = mul(DirectionalLight.ShadowVP, output.WorldPos);
    output.SpotLightShadowPosition = mul(SpotLight.ShadowVP, output.WorldPos);
    return output;
}
