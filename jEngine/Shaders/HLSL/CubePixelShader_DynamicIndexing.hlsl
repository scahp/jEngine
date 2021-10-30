// Need to set below options
// SM 5.1
// /enable_unbounded_descriptor_tables or Add D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES to D3DCompileFromFile's Flag1 parameter

struct MaterialConstants
{
    uint matIndex;
};

ConstantBuffer<MaterialConstants> matConstants : register(b0);

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
    return IN.Color + g_texture.Sample(g_sampler, IN.UV) * 0.1 + g_texture2[matConstants.matIndex].Sample(g_sampler, IN.UV);
}