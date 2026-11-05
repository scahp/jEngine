#include "common.hlsl"

#if USE_SPHERICAL_MAP
#include "Sphericalmap.hlsl"
#endif

#ifndef USE_VERTEX_COLOR
#define USE_VERTEX_COLOR 0
#endif

#ifndef USE_VERTEX_BITANGENT
#define USE_VERTEX_BITANGENT 0
#endif

struct VSInput
{
    [[vk::location((0))]] float3 Position : POSITION0;
#if USE_VERTEX_COLOR
    [[vk::location((1))]] float4 Color : COLOR0;
#endif
    [[vk::location((1+USE_VERTEX_COLOR))]] float3 Normal : NORMAL0;
    [[vk::location((2+USE_VERTEX_COLOR))]] float3 Tangent : TANGENT0;
#if USE_VERTEX_BITANGENT
    [[vk::location((3+USE_VERTEX_COLOR))]] float3 Bitangent : BITANGENT0;
#endif
    [[vk::location((3+USE_VERTEX_COLOR+USE_VERTEX_BITANGENT))]] float2 TexCoord : TEXCOORD0;
};

#include "Features/materialvertex.hlsl"

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 PrevPos : POSITION0;
    float3 LocalPos : POSITION1;
    float4 TestPos : POSITION2;
#if USE_VERTEX_COLOR
    float4 Color : COLOR0;
#endif
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL0;
    float4 WorldPos : TEXCOORD1;
    float3x3 TBN : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;

    JE_DeformWater(input.Position, input.Normal, input.Tangent);
    
    output.LocalPos = input.Position;
    output.WorldPos = mul(RenderObjectParam.M, float4(input.Position, 1.0));
    output.Pos = mul(ViewParam.VP, output.WorldPos);
    output.TestPos = mul(ViewParam.VP, output.WorldPos);
    output.PrevPos = mul(ViewParam.PrevVP, output.WorldPos);
#if USE_VERTEX_COLOR
    output.Color = input.Color;
#endif

    float2 UV = 0;
#if USE_SPHERICAL_MAP
    output.TexCoord = GetSphericalMap_TwoMirrorBall(normalize(input.Position));
#else
    output.TexCoord = input.TexCoord;
#endif
    output.Normal = normalize(mul(input.Normal, (float3x3)RenderObjectParam.InvM));
    
    #if USE_VERTEX_BITANGENT
    float3 Bitangent = input.Bitangent;
    #else
    float3 Bitangent = cross(input.Normal, input.Tangent);
    #endif // USE_VERTEX_BITANGENT
    // Heightfield V grows toward local +Z; imported meshes retain their authored
    // bitangent sign whenever water deformation is disabled.
    if (MaterialGraphParam.WaterControl.x > 0.5) Bitangent = -cross(input.Normal, input.Tangent);

    float3 N = output.Normal;
    float3 T = mul((float3x3)RenderObjectParam.M, input.Tangent).xyz;
    float3 B = mul((float3x3)RenderObjectParam.M, Bitangent).xyz;
    T -= N * dot(T, N);
    if (dot(T, T) < 1.0e-12) T = cross(abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), N);
    T = normalize(T);
    B = normalize(cross(N, T)) * (dot(cross(N, T), B) < 0.0 ? -1.0 : 1.0);
    output.TBN = float3x3(T, B, N);
    output.TBN = transpose(output.TBN);

    return output;
}
