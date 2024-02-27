struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

Texture2D Texture : register(t0, space0);
SamplerState TextureSampler : register(s0, space0);

float4 main(VSOutput input) : SV_TARGET
{
    float4 color = Texture.Sample(TextureSampler, input.TexCoord);
    color.xyz = pow(color.xyz, 1.0f / 2.2f);
    return color;
}
