#ifndef JENGINE_ADVANCED_SHADOWS_HLSL
#define JENGINE_ADVANCED_SHADOWS_HLSL 1

// Resource-free helpers shared by the raster producers and lighting readers.
// VSM/ESM follow scahp/Shadows (MIT), reference 87853997029283edfbbb60eaad03cf163c95f26c.
// EVSM extends that reference's positive warp with the negative exponential
// pair. All moment producers/readers use ordinary [0, 1] shadow depth.
#include "shadowfiltering.hlsl"

static const uint JE_SHADOW_DEPTH = 0;
static const uint JE_SHADOW_VSM = 1;
static const uint JE_SHADOW_ESM = 2;
static const uint JE_SHADOW_EVSM = 3;
static const uint JE_SHADOW_DEEP = 4;
static const uint JE_SHADOW_VOLUME = 5;
static const uint JE_SHADOW_PROJECTION_SINGLE = 0;
static const uint JE_SHADOW_PROJECTION_CSM = 1;
static const uint JE_SHADOW_PROJECTION_PSM = 2;
static const uint JE_SHADOW_PROJECTION_DUAL = 3;
static const uint JE_SHADOW_PROJECTION_CUBE_ATLAS = 4;
static const uint JE_DEEP_SHADOW_BIN_COUNT = 8;

float JE_ShadowExponent(float exponent)
{
    // exp(2 * 40) and its derivative variance remain finite in RGBA32F.
    return isfinite(exponent) ? clamp(exponent, 0.1, 40.0) : 20.0;
}

float JE_ReduceShadowLightBleeding(float visibility, float amount)
{
    const float reduction = isfinite(amount) ? clamp(amount, 0.0, 0.999) : 0.0;
    return saturate((visibility - reduction) / (1.0 - reduction));
}

float JE_ShadowChebyshev(float2 moments, float receiver, float minimumVariance, float bleed)
{
    if (!all(isfinite(moments)) || !isfinite(receiver)) return 1.0;
    if (receiver <= moments.x) return 1.0;
    const float variance = max(moments.y - moments.x * moments.x, max(minimumVariance, 1.0e-38));
    const float delta = receiver - moments.x;
    return JE_ReduceShadowLightBleeding(variance / (variance + delta * delta), bleed);
}

float4 JE_EncodeShadowMoments(float depth, uint mode, float positiveExponent, float negativeExponent)
{
    const float d = saturate(depth);
    if (mode == JE_SHADOW_DEPTH) return float4(d, 0.0, 0.0, 1.0);
    if (mode == JE_SHADOW_VSM) return float4(d, d * d, 0.0, 0.0);
    const float k = JE_ShadowExponent(positiveExponent);
    const float positive = exp(k * d);
    if (mode == JE_SHADOW_ESM) return float4(positive, 0.0, 0.0, 0.0);
    const float negative = -exp(-JE_ShadowExponent(negativeExponent) * d);
    return float4(positive, positive * positive, negative, negative * negative);
}

float4 JE_EncodeShadowMoments(float depth, float2 depthGradient, uint mode,
    float positiveExponent, float negativeExponent)
{
    float4 moments = JE_EncodeShadowMoments(depth, mode, positiveExponent, negativeExponent);
    const float derivativeVariance = 0.25 * dot(depthGradient, depthGradient);
    if (mode == JE_SHADOW_VSM)
        moments.y += derivativeVariance;
    else if (mode == JE_SHADOW_EVSM)
    {
        const float positiveDerivative = JE_ShadowExponent(positiveExponent) * moments.x;
        const float negativeDerivative = -JE_ShadowExponent(negativeExponent) * moments.z;
        moments.y += derivativeVariance * positiveDerivative * positiveDerivative;
        moments.w += derivativeVariance * negativeDerivative * negativeDerivative;
    }
    return moments;
}

float JE_MomentShadowVisibility(float4 moments, float receiverDepth, uint mode,
    float positiveExponent, float negativeExponent, float minimumVariance, float bleed)
{
    if (!all(isfinite(moments)) || !isfinite(receiverDepth)) return 1.0;
    const float d = saturate(receiverDepth);
    const float variance = isfinite(minimumVariance) ? clamp(minimumVariance, 1.0e-8, 1.0) : 1.0e-5;
    if (mode == JE_SHADOW_VSM)
        return JE_ShadowChebyshev(moments.xy, d, variance, bleed);
    const float k = JE_ShadowExponent(positiveExponent);
    if (mode == JE_SHADOW_ESM)
        return saturate(moments.x * exp(-k * d));
    if (mode != JE_SHADOW_EVSM) return 1.0;
    const float positive = exp(k * d);
    const float negativeK = JE_ShadowExponent(negativeExponent);
    const float negative = -exp(-negativeK * d);
    // Scale the depth-domain variance floor by the warp derivative squared.
    const float positiveDerivative = k * positive;
    const float negativeDerivative = -negativeK * negative;
    const float positiveVisibility = JE_ShadowChebyshev(moments.xy, positive,
        variance * positiveDerivative * positiveDerivative, bleed);
    const float negativeVisibility = JE_ShadowChebyshev(moments.zw, negative,
        variance * negativeDerivative * negativeDerivative, bleed);
    return min(positiveVisibility, negativeVisibility);
}

bool JE_ProjectShadow(float4x4 shadowVP, float3 worldPosition, out float3 uvDepth)
{
    const float4 projected = mul(shadowVP, float4(worldPosition, 1.0));
    uvDepth = 0.0;
    if (!all(isfinite(projected)) || projected.w <= 1.0e-7) return false;
    const float3 ndc = projected.xyz / projected.w;
    uvDepth = float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
    return all(uvDepth.xy >= 0.0) && all(uvDepth.xy <= 1.0)
        && uvDepth.z >= 0.0 && uvDepth.z <= 1.0;
}

uint JE_SelectShadowCascade(float4 splitDepths, uint cascadeCount, float positiveViewDepth)
{
    const uint count = clamp(cascadeCount, 1u, 4u);
    uint index = 0;
    [unroll] for (uint i = 0; i < 3; ++i)
        if (i + 1 < count && positiveViewDepth > splitDepths[i]) index = i + 1;
    return index;
}

// Matches the existing cube cameras: +X, -X, +Y, -Y, +Z, -Z. The selected
// camera matrix still owns the exact face orientation and projected depth.
uint JE_ShadowCubeFace(float3 relativePosition)
{
    const float3 magnitude = abs(relativePosition);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
        return relativePosition.x >= 0.0 ? 0u : 1u;
    if (magnitude.y >= magnitude.z)
        return relativePosition.y >= 0.0 ? 2u : 3u;
    return relativePosition.z >= 0.0 ? 4u : 5u;
}

// xy = atlas origin, zw = atlas scale. CSM uses 2x2, Dual uses 2x1.
float4 JE_ShadowAtlasRect(uint index, uint2 grid)
{
    const uint2 safeGrid = max(grid, uint2(1, 1));
    const uint slot = min(index, safeGrid.x * safeGrid.y - 1);
    const float2 scale = 1.0 / float2(safeGrid);
    return float4(float2(slot % safeGrid.x, slot / safeGrid.x) * scale, scale);
}

float JE_CompareShadowTexel(Texture2D shadowMap, int2 pixel, float receiver)
{
    const float storedDepth = shadowMap.Load(int3(pixel, 0)).r;
    return !isfinite(storedDepth) || receiver <= storedDepth ? 1.0 : 0.0;
}

float JE_CompareShadowBilinear(Texture2D shadowMap, float2 uv, float receiver,
    uint2 size, int2 firstPixel, int2 lastPixel)
{
    const float2 pixel = uv * float2(size) - 0.5;
    const int2 base = int2(floor(pixel));
    const float2 weight = frac(pixel);
    const float a = JE_CompareShadowTexel(shadowMap, clamp(base, firstPixel, lastPixel), receiver);
    const float b = JE_CompareShadowTexel(shadowMap, clamp(base + int2(1, 0), firstPixel, lastPixel), receiver);
    const float c = JE_CompareShadowTexel(shadowMap, clamp(base + int2(0, 1), firstPixel, lastPixel), receiver);
    const float d = JE_CompareShadowTexel(shadowMap, clamp(base + int2(1, 1), firstPixel, lastPixel), receiver);
    return lerp(lerp(a, b, weight.x), lerp(c, d, weight.x), weight.y);
}

float JE_SampleShadowRect(Texture2D shadowMap,
    float3 localUVDepth, float4 rect, float depthBias, float radiusTexels, int filterMode)
{
    if (!all(isfinite(localUVDepth)) || any(localUVDepth.xy < 0.0) || any(localUVDepth.xy > 1.0)
        || localUVDepth.z < 0.0 || localUVDepth.z > 1.0) return 1.0;
    uint width, height;
    shadowMap.GetDimensions(width, height);
    const float2 invSize = 1.0 / float2(max(width, 1u), max(height, 1u));
    const float2 lower = rect.xy + invSize * 0.5;
    const float2 upper = max(lower, rect.xy + rect.zw - invSize * 0.5);
    const int2 firstPixel = int2(rect.xy * float2(width, height));
    const int2 lastPixel = max(firstPixel, int2((rect.xy + rect.zw) * float2(width, height)) - 1);
    const float2 uv = clamp(rect.xy + localUVDepth.xy * rect.zw, lower, upper);
    const float receiver = saturate(localUVDepth.z - depthBias);
    if (filterMode != 1 || !isfinite(radiusTexels) || radiusTexels <= 0.0)
        return JE_CompareShadowBilinear(shadowMap, uv, receiver, uint2(width, height), firstPixel, lastPixel);
    float visibility = 0.0;
    const float2 radius = min(radiusTexels, 4.0) * invSize;
    [unroll] for (uint i = 0; i < 16; ++i)
        visibility += JE_CompareShadowBilinear(shadowMap,
            clamp(uv + ShadowPoisson16[i] * radius, lower, upper), receiver,
            uint2(width, height), firstPixel, lastPixel);
    return visibility * (1.0 / 16.0);
}

float JE_SampleCascadeShadow(Texture2D shadowMap,
    float3 worldPosition, float positiveViewDepth, float4x4 shadowVP[6], float4 splitDepths,
    uint cascadeCount, float depthBias, float radiusTexels, int filterMode, float blendFraction)
{
    const uint count = clamp(cascadeCount, 1u, 4u);
    if (!isfinite(positiveViewDepth) || positiveViewDepth > splitDepths[count - 1]) return 1.0;
    const uint cascade = JE_SelectShadowCascade(splitDepths, count, positiveViewDepth);
    float3 projected;
    float visibility = JE_ProjectShadow(shadowVP[cascade], worldPosition, projected)
        ? JE_SampleShadowRect(shadowMap, projected,
            JE_ShadowAtlasRect(cascade, uint2(2, 2)), depthBias, radiusTexels, filterMode) : 1.0;
    if (cascade + 1 < count && blendFraction > 0.0)
    {
        const float startDepth = cascade == 0 ? 0.0 : splitDepths[cascade - 1];
        const float interval = max(splitDepths[cascade] - startDepth, 1.0e-6);
        const float blendWidth = interval * min(blendFraction, 0.5);
        const float blend = saturate((positiveViewDepth - splitDepths[cascade] + blendWidth) / max(blendWidth, 1.0e-6));
        if (blend > 0.0)
        {
            const float nextVisibility = JE_ProjectShadow(shadowVP[cascade + 1], worldPosition, projected)
                ? JE_SampleShadowRect(shadowMap, projected,
                    JE_ShadowAtlasRect(cascade + 1, uint2(2, 2)), depthBias, radiusTexels, filterMode) : 1.0;
            visibility = lerp(visibility, nextVisibility, blend);
        }
    }
    return visibility;
}

float JE_DeepShadowOpticalDepth(float opacity, float maximumOpticalDepth)
{
    const float limit = clamp(maximumOpticalDepth, 0.01, 16.0);
    // Alpha one is a finite fully blocking layer. Additive geometry blending
    // remains defined, and a reader clamps the accumulated optical depth.
    return min(-log(max(1.0 - saturate(opacity), exp(-limit))), limit);
}

void JE_EncodeDeepShadowBins(float depth, float opacity, float maximumOpticalDepth,
    out float4 bins0, out float4 bins1)
{
    bins0 = 0.0;
    bins1 = 0.0;
    const uint bin = min((uint)(saturate(depth) * JE_DEEP_SHADOW_BIN_COUNT), JE_DEEP_SHADOW_BIN_COUNT - 1);
    const float opticalDepth = JE_DeepShadowOpticalDepth(opacity, maximumOpticalDepth);
    if (bin < 4) bins0[bin] = opticalDepth;
    else bins1[bin - 4] = opticalDepth;
}

float JE_DeepShadowTransmittance(float4 bins0, float4 bins1, float receiverDepth, float maximumOpticalDepth)
{
    // Eight uniform depth bins approximate the distribution within the active
    // bin linearly. These are optical-depth impulses, not cumulative channels.
    const float end = saturate(receiverDepth) * JE_DEEP_SHADOW_BIN_COUNT;
    float opticalDepth = 0.0;
    [unroll] for (uint i = 0; i < 4; ++i)
    {
        const float weight0 = saturate(end - float(i));
        const float weight1 = saturate(end - float(i + 4));
        // An extremely dense RGBA16F bin may saturate to infinity. Do not
        // multiply that unused bin by zero before clamping total optical depth.
        if (weight0 > 0.0) opticalDepth += max(bins0[i], 0.0) * weight0;
        if (weight1 > 0.0) opticalDepth += max(bins1[i], 0.0) * weight1;
    }
    return exp(-min(opticalDepth, clamp(maximumOpticalDepth, 0.01, 32.0)));
}

float2 JE_ClampedShadowAtlasUV(Texture2D shadowMap, float2 localUV, float4 rect)
{
    uint width, height;
    shadowMap.GetDimensions(width, height);
    const float2 halfTexel = 0.5 / float2(max(width, 1u), max(height, 1u));
    const float2 lower = rect.xy + halfTexel;
    return clamp(rect.xy + localUV * rect.zw, lower, max(lower, rect.xy + rect.zw - halfTexel));
}

float4 JE_FilterDeepShadowBins(Texture2D shadowMap, float2 localUV, float4 rect, float maximumOpticalDepth)
{
    uint width, height;
    shadowMap.GetDimensions(width, height);
    const float2 uv = JE_ClampedShadowAtlasUV(shadowMap, localUV, rect);
    const float2 pixel = uv * float2(width, height) - 0.5;
    const int2 base = int2(floor(pixel));
    const float2 weight = frac(pixel);
    const int2 firstPixel = int2(rect.xy * float2(width, height));
    const int2 lastPixel = max(firstPixel, int2((rect.xy + rect.zw) * float2(width, height)) - 1);
    const float limit = clamp(maximumOpticalDepth, 0.01, 32.0);
    // Clamp before interpolation to keep even saturated half-float bins from
    // introducing infinity * zero at a bilinear sample boundary.
    const float4 a = clamp(shadowMap.Load(int3(clamp(base, firstPixel, lastPixel), 0)), 0.0, limit);
    const float4 b = clamp(shadowMap.Load(int3(clamp(base + int2(1, 0), firstPixel, lastPixel), 0)), 0.0, limit);
    const float4 c = clamp(shadowMap.Load(int3(clamp(base + int2(0, 1), firstPixel, lastPixel), 0)), 0.0, limit);
    const float4 d = clamp(shadowMap.Load(int3(clamp(base + int2(1, 1), firstPixel, lastPixel), 0)), 0.0, limit);
    return lerp(lerp(a, b, weight.x), lerp(c, d, weight.x), weight.y);
}

float4 JE_FilterShadowMoments(Texture2D shadowMap, float2 localUV, float4 rect)
{
    // RGBA32F need not advertise hardware linear filtering on every target.
    // Explicit four-texel interpolation requires only ordinary sampled loads.
    uint width, height;
    shadowMap.GetDimensions(width, height);
    const float2 uv = JE_ClampedShadowAtlasUV(shadowMap, localUV, rect);
    const float2 pixel = uv * float2(width, height) - 0.5;
    const int2 base = int2(floor(pixel));
    const float2 weight = frac(pixel);
    const int2 firstPixel = int2(rect.xy * float2(width, height));
    const int2 lastPixel = max(firstPixel, int2((rect.xy + rect.zw) * float2(width, height)) - 1);
    const float4 a = shadowMap.Load(int3(clamp(base, firstPixel, lastPixel), 0));
    const float4 b = shadowMap.Load(int3(clamp(base + int2(1, 0), firstPixel, lastPixel), 0));
    const float4 c = shadowMap.Load(int3(clamp(base + int2(0, 1), firstPixel, lastPixel), 0));
    const float4 d = shadowMap.Load(int3(clamp(base + int2(1, 1), firstPixel, lastPixel), 0));
    return lerp(lerp(a, b, weight.x), lerp(c, d, weight.x), weight.y);
}

float JE_SampleAdvancedShadowRect(Texture2D shadowMap, Texture2D deepShadow0, Texture2D deepShadow1,
    SamplerState filtering, float3 localUVDepth, float4 rect, uint representation,
    float positiveExponent, float negativeExponent, float minimumVariance, float bleed,
    float depthBias, float radiusTexels, int filterMode, float maximumOpticalDepth)
{
    if (!all(isfinite(localUVDepth)) || any(localUVDepth.xy < 0.0) || any(localUVDepth.xy > 1.0)
        || localUVDepth.z < 0.0 || localUVDepth.z > 1.0) return 1.0;
    if (representation == JE_SHADOW_DEPTH)
        return JE_SampleShadowRect(shadowMap, localUVDepth, rect, depthBias, radiusTexels, filterMode);
    const float receiver = saturate(localUVDepth.z - depthBias);
    if (representation == JE_SHADOW_DEEP)
        return JE_DeepShadowTransmittance(
            JE_FilterDeepShadowBins(deepShadow0, localUVDepth.xy, rect, maximumOpticalDepth),
            JE_FilterDeepShadowBins(deepShadow1, localUVDepth.xy, rect, maximumOpticalDepth),
            receiver, maximumOpticalDepth);
    const float2 uv = JE_ClampedShadowAtlasUV(shadowMap, localUVDepth.xy, rect);
    if (representation == JE_SHADOW_VOLUME) return saturate(shadowMap.SampleLevel(filtering, uv, 0.0).r);
    const float4 sampled = JE_FilterShadowMoments(shadowMap, localUVDepth.xy, rect);
    return JE_MomentShadowVisibility(sampled, receiver, representation,
        positiveExponent, negativeExponent, minimumVariance, bleed);
}

float JE_DualParaboloidSeam(float seamWidth)
{
    return isfinite(seamWidth) ? clamp(seamWidth, 0.0, 0.125) : 0.02;
}

float3 JE_DualParaboloidDirection(float3 relativePosition)
{
    return relativePosition * rsqrt(max(dot(relativePosition, relativePosition), 1.0e-12));
}

float4 JE_DualParaboloidClipPosition(float3 relativePosition, uint hemisphere,
    float nearDistance, float farDistance, float seamWidth)
{
    const float radius = length(relativePosition);
    const float3 direction = JE_DualParaboloidDirection(relativePosition);
    const float signZ = hemisphere == 0 ? 1.0 : -1.0;
    const float denominator = max(1.0 + signZ * direction.z + JE_DualParaboloidSeam(seamWidth), 1.0e-4);
    const float depth = (radius - nearDistance) / max(farDistance - nearDistance, 1.0e-6);
    return float4(direction.xy / denominator, depth, 1.0);
}

float JE_SampleDualParaboloidShadow(Texture2D shadowMap,
    float3 worldPosition, float3 lightPosition, float nearDistance, float farDistance,
    float seamWidth, float depthBias, float radiusTexels, int filterMode)
{
    const float3 relative = worldPosition - lightPosition;
    const float radius = length(relative);
    if (!isfinite(radius) || radius <= nearDistance || radius > farDistance) return 1.0;
    const float3 direction = JE_DualParaboloidDirection(relative);
    const uint hemisphere = direction.z >= 0.0 ? 0u : 1u;
    const float4 projected = JE_DualParaboloidClipPosition(relative, hemisphere, nearDistance, farDistance, seamWidth);
    float3 uvDepth = float3(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5, projected.z);
    float visibility = JE_SampleShadowRect(shadowMap, uvDepth,
        JE_ShadowAtlasRect(hemisphere, uint2(2, 1)), depthBias, radiusTexels, filterMode);
    // The overlap is generated in both hemispheres. Conservative selection at
    // the equator prevents a filter tap from exposing an uncleared atlas tile.
    if (abs(direction.z) <= JE_DualParaboloidSeam(seamWidth))
    {
        const uint other = 1u - hemisphere;
        const float4 otherProjected = JE_DualParaboloidClipPosition(relative, other, nearDistance, farDistance, seamWidth);
        uvDepth = float3(otherProjected.x * 0.5 + 0.5, 0.5 - otherProjected.y * 0.5, otherProjected.z);
        visibility = min(visibility, JE_SampleShadowRect(shadowMap, uvDepth,
            JE_ShadowAtlasRect(other, uint2(2, 1)), depthBias, radiusTexels, filterMode));
    }
    return visibility;
}

#endif
