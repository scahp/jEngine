
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 main(VSOutput input) : SV_TARGET
{
    float t = Texture.Sample(TextureSampler, input.TexCoord).x * ApplyParam.Intensity;
    return float4(t, t, t, 0);
}
