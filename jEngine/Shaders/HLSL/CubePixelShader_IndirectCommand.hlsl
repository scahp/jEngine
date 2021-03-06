
struct MaterialConstants
{
    uint matIndex;
};
ConstantBuffer<MaterialConstants> matConstants : register(b0);

cbuffer InstanceConstantBuffer : register(b1)
{
    float4 InstanceColor;
};

struct PixelShaderInput
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

Texture2D g_texture : register(t0);
Texture2D g_texture2[] : register(t1);
SamplerState g_sampler : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    return InstanceColor + g_texture.Sample(g_sampler, IN.UV) + g_texture2[matConstants.matIndex].Sample(g_sampler, IN.UV);
}
