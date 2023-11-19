struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 TexCoord[8] : TEXCOORD0;
};

struct BloomUniformBuffer
{
    float4 BufferSizeAndInvSize;
    float4 TintA;
    float4 TintB;
    float BloomIntensity;
};

cbuffer BloomParam : register(b0, space0) { BloomUniformBuffer BloomParam; }
Texture2D Texture : register(t1, space0);
SamplerState TextureSampler : register(s1, space0);

// bloom up with gaussian blur from UE5
float4 main(VSOutput input) : SV_TARGET
{
    const float WA = (1.0f / 8.0f) * BloomParam.TintA;
    const float WB = (1.0f / 8.0f) * BloomParam.TintB;

    half3 A0 = Texture.Sample(TextureSampler, input.TexCoord[0].xy).rgb * WA;
    half3 A1 = Texture.Sample(TextureSampler, input.TexCoord[0].zw).rgb * WA;
    half3 A2 = Texture.Sample(TextureSampler, input.TexCoord[1].xy).rgb * WA;
    half3 A3 = Texture.Sample(TextureSampler, input.TexCoord[1].zw).rgb * WA;
    half3 A4 = Texture.Sample(TextureSampler, input.TexCoord[2].xy).rgb * WA;
    half3 A5 = Texture.Sample(TextureSampler, input.TexCoord[2].zw).rgb * WA;
    half3 A6 = Texture.Sample(TextureSampler, input.TexCoord[3].xy).rgb * WA;
    half3 A7 = Texture.Sample(TextureSampler, input.TexCoord[3].zw).rgb * WA;

    half3 B0 = Texture.Sample(TextureSampler, input.TexCoord[3].zw).rgb * WB;
    half3 B1 = Texture.Sample(TextureSampler, input.TexCoord[4].xy).rgb * WB;
    half3 B2 = Texture.Sample(TextureSampler, input.TexCoord[4].zw).rgb * WB;
    half3 B3 = Texture.Sample(TextureSampler, input.TexCoord[5].xy).rgb * WB;
    half3 B4 = Texture.Sample(TextureSampler, input.TexCoord[5].zw).rgb * WB;
    half3 B5 = Texture.Sample(TextureSampler, input.TexCoord[6].xy).rgb * WB;
    half3 B6 = Texture.Sample(TextureSampler, input.TexCoord[6].zw).rgb * WB;
    half3 B7 = Texture.Sample(TextureSampler, input.TexCoord[7].xy).rgb * WB;

    return float4(A0 + A1 + A2 + A3 + A4 + A5 + A6 + A7 + B0 + B1 + B2 + B3 + B4 + B5 + B6 + B7, 0.0);
}
