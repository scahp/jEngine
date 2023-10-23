struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 TexCoord[8] : TEXCOORD0;
};

Texture2D Texture : register(t0, space0);
SamplerState TextureSampler : register(s0, space0);

// bloom down with gaussian blur from UE5
float4 main(VSOutput input) : SV_TARGET
{
    const float W = 1.0 / 15.0;

    half4 N0 = Texture.Sample(TextureSampler, input.TexCoord[0].xy) * W;
    half4 N1 = Texture.Sample(TextureSampler, input.TexCoord[0].zw) * W;
    half4 N2 = Texture.Sample(TextureSampler, input.TexCoord[1].xy) * W;
    half4 N3 = Texture.Sample(TextureSampler, input.TexCoord[1].zw) * W;
    half4 N4 = Texture.Sample(TextureSampler, input.TexCoord[2].xy) * W;
    half4 N5 = Texture.Sample(TextureSampler, input.TexCoord[2].zw) * W;
    half4 N6 = Texture.Sample(TextureSampler, input.TexCoord[3].xy) * W;
    half4 N7 = Texture.Sample(TextureSampler, input.TexCoord[3].zw) * W;
    half4 N8 = Texture.Sample(TextureSampler, input.TexCoord[4].xy) * W;
    half4 N9 = Texture.Sample(TextureSampler, input.TexCoord[4].zw) * W;
    half4 N10 = Texture.Sample(TextureSampler, input.TexCoord[5].xy) * W;
    half4 N11 = Texture.Sample(TextureSampler, input.TexCoord[5].zw) * W;
    half4 N12 = Texture.Sample(TextureSampler, input.TexCoord[6].xy) * W;
    half4 N13 = Texture.Sample(TextureSampler, input.TexCoord[6].zw) * W;
    half4 N14 = Texture.Sample(TextureSampler, input.TexCoord[7].xy) * W;

    float4 color = (N0 + N1 + N2 + N3 + N4 + N5 + N6 + N7 + N8 + N9 + N10 + N11 + N12 + N13 + N14);
    return color;
}
