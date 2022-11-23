struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

Texture2D Texture : register(t0, space0);
SamplerState TextureSampler : register(s0, space0);

float Luminance(float3 LinearColor)
{
    return dot(LinearColor, float3(0.3, 0.59, 0.11));
}

// bloom setup from UE5
float4 main(VSOutput input) : SV_TARGET
{
    float4 SceneColor = Texture.Sample(TextureSampler, input.TexCoord);

    // clamp to avoid artifacts from exceeding fp16 through framebuffer blending of multiple very bright lights
    SceneColor.rgb = min(float3(256 * 256, 256 * 256, 256 * 256), SceneColor.rgb);

    half3 LinearColor = SceneColor.rgb;

    float ExposureScale = 1.0f;     // todo apply eye adaptation.
    float BloomThreshold = 0.0f;

    half TotalLuminance = Luminance(LinearColor) * ExposureScale;
    half BloomLuminance = TotalLuminance - BloomThreshold;
    half BloomAmount = saturate(BloomLuminance * 0.5f);

    return float4(BloomAmount * LinearColor, 0);
}
