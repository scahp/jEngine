struct PixelShaderInput
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

Texture2D g_texture : register(t0);
Texture2D g_texture2[3] : register(t1);
SamplerState g_sampler : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    //return IN.Color + g_texture.Sample(g_sampler, IN.UV) * 0.1 + g_texture2.Sample(g_sampler, IN.UV) * 0.9;
    return IN.Color + g_texture.Sample(g_sampler, IN.UV) * 0.1 + g_texture2[0].Sample(g_sampler, IN.UV) * 0.3
    + g_texture2[1].Sample(g_sampler, IN.UV) * 0.3 + g_texture2[2].Sample(g_sampler, IN.UV) * 0.3;
}