#ifndef JENGINE_EFFECT_SAMPLING_HLSL
#define JENGINE_EFFECT_SAMPLING_HLSL

// Sample the matching pixel center; one-pixel axes remain finite and centered.
float2 EffectPixelUV(uint2 Pixel, uint2 Extent)
{
    return float2((float(Pixel.x) + 0.5f) / float(Extent.x),
                  (float(Pixel.y) + 0.5f) / float(Extent.y));
}

#endif
