// Leave inactive materials in the existing HDR target, including their exact
// native-format bits. They must not pay a half-float roundtrip for another
// material's diffusion. The render pass loads and preserves discarded pixels.
float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    const int3 pixel = int3(int2(position.xy), 0);
    const float4 mask = CopyMask.Load(pixel);
    if (mask.a < 1.0 || !any(mask.rgb > 0.0) || CopyDiffuse.Load(pixel).a <= 0.0) discard;
    return CopyColor.Load(pixel);
}
