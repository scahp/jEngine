#include "common.hlsl"
// Advanced shadow shader injection point; declarations precede helper bodies.
#include "Features/advancedshadows.hlsl"

#ifndef JE_ADVANCED_SHADOW_PASS
#define JE_ADVANCED_SHADOW_PASS 0
#endif

float JE_ReadAdvancedShadowDepth(int2 pixel)
{
    const float depth = ShadowInput.Load(int3(pixel, 0)).r;
    return isfinite(depth) ? saturate(depth) : 1.0;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 size = uint2(AdvancedShadow.MapSizeAndInvSize.xy);
    const uint2 pixel = dispatchThreadID.xy;
    if (any(pixel >= size)) return;
    const uint2 grid = max(uint2(AdvancedShadow.AtlasParams.xy), uint2(1, 1));
    const uint2 tileSize = max(size / grid, uint2(1, 1));
    const int2 firstPixel = int2((pixel / tileSize) * tileSize);
    const int2 lastPixel = min(firstPixel + int2(tileSize) - 1, int2(size) - 1);
    const int2 p = int2(pixel);

#if JE_ADVANCED_SHADOW_PASS == 0
    const uint representation = (uint)AdvancedShadow.ModeParams.x;
    const float depth = JE_ShadowMomentDepth(JE_ReadAdvancedShadowDepth(p), representation,
        (uint)AdvancedShadow.ModeParams.y, (uint)AdvancedShadow.CameraPositionAndType.w,
        AdvancedShadow.LightPositionAndNear.w, AdvancedShadow.FarAndDeepParams.x);
    // This image stores already rasterized nearest depths. Neighbor differences
    // cross silhouettes/clear texels and are not a triangle's ddx/ddy; adding
    // them as slope variance causes severe light bleeding. Encode point moments
    // here; the following filter computes the actual neighborhood distribution.
    ShadowResult[pixel] = JE_EncodeShadowMoments(depth, representation,
        AdvancedShadow.FarAndDeepParams.y, AdvancedShadow.FarAndDeepParams.z);
#elif JE_ADVANCED_SHADOW_PASS == 1 || JE_ADVANCED_SHADOW_PASS == 2
    const int radius = clamp((int)AdvancedShadow.FilterParams.w, 0, 8);
    const float sigma = max(float(radius) * 0.5, 0.5);
    float totalWeight = 0.0;
    float4 moments = 0.0;
    [unroll] for (int i = -8; i <= 8; ++i)
    {
        if (abs(i) > radius) continue;
        const float weight = exp(-0.5 * float(i * i) / (sigma * sigma));
#if JE_ADVANCED_SHADOW_PASS == 1
        const int2 neighbor = clamp(p + int2(i, 0), firstPixel, lastPixel);
#else
        const int2 neighbor = clamp(p + int2(0, i), firstPixel, lastPixel);
#endif
        moments += ShadowInput.Load(int3(neighbor, 0)) * weight;
        totalWeight += weight;
    }
    ShadowResult[pixel] = moments / max(totalWeight, 1.0e-6);
#else
#error JE_ADVANCED_SHADOW_PASS must be 0 (encode), 1 (horizontal blur), or 2 (vertical blur).
#endif
}
