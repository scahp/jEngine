struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

Texture2D Texture : register(t0, space0);
SamplerState TextureSampler : register(s0, space0);
Texture2D PreEyeAdaptionTexture : register(t1, space0);
SamplerState PreEyeAdaptionTextureSampler : register(s1, space0);

float GetLuminance(float3 LinearColor)
{
    return dot(LinearColor, float3(0.3, 0.59, 0.11));
}

// bloom setup from UE5
float4 main(VSOutput input) : SV_TARGET
{
    float4 SceneColor = Texture.Sample(TextureSampler, input.TexCoord);

    // Bloom setup
    // clamp to avoid artifacts from exceeding fp16 through framebuffer blending of multiple very bright lights
    SceneColor.rgb = min(float3(256 * 256, 256 * 256, 256 * 256), SceneColor.rgb);

    float ExposureScale = PreEyeAdaptionTexture.Sample(PreEyeAdaptionTextureSampler, float2(0, 0));
    float BloomThreshold = 0.0f;

    half3 SceneColorLuminance = GetLuminance(SceneColor.rgb);

    half TotalLuminance = SceneColorLuminance * ExposureScale;
    half BloomLuminance = TotalLuminance - BloomThreshold;
    half BloomAmount = saturate(BloomLuminance * 0.5f);

    // This code is based on UE5 EyeAdaptation Basic Method
    // Eye adaptation setup
    float Luminance = max(SceneColorLuminance, 0.0001);
    float LogLuminance = clamp(log2(Luminance), -10.0, 20.0);
    //////////////////////////////////////////////////////////////////////////

    return float4(BloomAmount * SceneColor.rgb, LogLuminance);
}
