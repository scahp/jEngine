struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float4 Color : COLOR0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float3 Tangent : NORMAL1;
    [[vk::location(4)]] float2 TexCoord : TEXCOORD0;
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

cbuffer ViewParam : register(b0, space0) { ViewUniformBuffer ViewParam; }
cbuffer RenderObjectParam : register(b0, space1) { RenderObjectUniformBuffer RenderObjectParam; }

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    
    output.Pos = mul(ViewParam.VP, mul(RenderObjectParam.M, float4(input.Position, 1.0)));
    output.TexCoord = input.TexCoord;
    return output;
}
