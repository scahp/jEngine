struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct ViewUniformBuffer
{
    float4x4 V;
    float4x4 P;
    float4x4 VP;
    float3 EyeWorld;
    float padding0;
};

struct RenderObjectUniformBuffer
{
    float4x4 M;
    float4x4 InvM;
};

cbuffer ViewParam : register(b0,space0) { ViewUniformBuffer ViewParam; }
cbuffer RenderObjectParam : register(b0, space1) { RenderObjectUniformBuffer RenderObjectParam; }
Texture2D DiffuseTexture : register(t0, space2);
SamplerState DiffuseTextureSampler : register(s0, space2);

float4 main(VSOutput input) : SV_TARGET
{
    float4 DiffuseColor = DiffuseTexture.Sample(DiffuseTextureSampler, input.TexCoord.xy);
    return DiffuseColor;
}
