struct PixelShaderInput
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 main(PixelShaderInput IN) : SV_Target
{
    return IN.Color + g_texture.Sample(g_sampler, IN.UV);
}