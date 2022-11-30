struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

Texture2D BloomTexture : register(t0, space0);
SamplerState BloomTextureSampler : register(s0, space0);
Texture2D Texture : register(t1, space0);
SamplerState TextureSampler : register(s1, space0);
Texture2D EyeAdaptationTexture : register(t2, space0);
SamplerState EyeAdaptationTextureSampler : register(s2, space0);

// http://filmicworlds.com/blog/filmic-tonemapping-with-piecewise-power-curves/		// wait for implementation
// Applies the filmic curve from John Hable's presentation
float3 FilmicToneMapALU(float3 linearColor)
{
    float3 color = max(0.0, linearColor - 0.004);
    color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);

    // result has 1/2.2 baked in
    return pow(color, 2.2);
}

float GetExposureScale()
{
    return EyeAdaptationTexture.Sample(EyeAdaptationTextureSampler, float2(0, 0)).x;
}

float4 main(VSOutput input) : SV_TARGET
{
    float4 SceneColor = Texture.Sample(TextureSampler, input.TexCoord);

    // Apply Exposure
    SceneColor.rgb *= GetExposureScale();

    float BloomMagnitude = 1.0f;    // todo apply eye adaptation.
    float4 BloomColor = BloomTexture.Sample(BloomTextureSampler, input.TexCoord);
    float4 color = SceneColor + BloomColor * BloomMagnitude;

    return float4(pow(color.rgb, 1.0 / 2.2), 0.0);
}
