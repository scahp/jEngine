#include "common.hlsl"
#ifndef USE_VERTEX_COLOR
#define USE_VERTEX_COLOR 0
#endif
#ifndef USE_VERTEX_BITANGENT
#define USE_VERTEX_BITANGENT 0
#endif
#ifndef JE_TEXTURE_SSS_CAPTURE
#define JE_TEXTURE_SSS_CAPTURE 0
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
    [[vk::location((3+USE_VERTEX_COLOR+USE_VERTEX_BITANGENT))]] float2 UV : TEXCOORD0;
};
struct TextureSSSVarying
{
    float4 Position : SV_POSITION;
    float3 World : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float3 Normal : TEXCOORD2;
    float3x3 TBN : TEXCOORD3;
    float4 Color : COLOR0;
};
TextureSSSVarying main(VSInput input)
{
    TextureSSSVarying output = (TextureSSSVarying)0;
    output.World = mul(RenderObjectParam.M,float4(input.Position,1.0)).xyz;
#if JE_TEXTURE_SSS_CAPTURE
    output.Position = mul(TextureSSS.LightVP,float4(output.World,1.0));
#else
    // A separate viewport selects the material's atlas tile. No camera Hi-Z
    // indirect argument is reused: camera-hidden surfaces still transport light.
    output.Position = float4(input.UV*float2(2.0,-2.0)+float2(-1.0,1.0),0.0,1.0);
#endif
    output.UV=input.UV;
    output.Normal=normalize(mul(input.Normal,(float3x3)RenderObjectParam.InvM));
#if USE_VERTEX_BITANGENT
    const float3 localB=input.Bitangent;
#else
    const float3 localB=cross(input.Normal,input.Tangent);
#endif
    const float3 N=output.Normal;
    float3 T=mul((float3x3)RenderObjectParam.M,input.Tangent);
    const float3 transformedB=mul((float3x3)RenderObjectParam.M,localB);
    T-=N*dot(T,N);
    if (dot(T,T)<1.0e-12) T=cross(abs(N.y)<.99?float3(0,1,0):float3(1,0,0),N);
    T=normalize(T);
    const float3 B=normalize(cross(N,T))*(dot(cross(N,T),transformedB)<0.0?-1.0:1.0);
    output.TBN=transpose(float3x3(T,B,N));
    output.Color=1.0;
#if USE_VERTEX_COLOR
    output.Color=input.Color;
#endif
    return output;
}
