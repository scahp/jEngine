#include "common.hlsl"
// Subsurface shader injection point; generated declarations precede helpers.
#include "Features/subsurface.hlsl"

#ifndef JE_SUBSURFACE_PASS
#define JE_SUBSURFACE_PASS 0
#endif

bool JE_SubsurfaceActive(float4 mask, float thickness, out uint profile)
{
    return JE_SubsurfaceProfile(mask.a, profile) && all(isfinite(mask)) &&
        any(mask.rgb > 0.0) && isfinite(thickness) && thickness > 0.0;
}

float4 JE_SubsurfaceFilter(int2 pixel, int2 axis)
{
    const float4 source = SSSInputDiffuse.Load(int3(pixel, 0));
    const float4 original = SSSOriginalDiffuse.Load(int3(pixel, 0));
    const float4 mask = SSSMask.Load(int3(pixel, 0));
    uint profile;
    if (!JE_SubsurfaceActive(mask, original.a, profile) || !all(isfinite(source)))
        return source;

    const float2 uv = (float2(pixel) + 0.5) * SSSView.SizeAndInvSize.zw;
    const float depth = SSSDepth.Load(int3(pixel, 0)).r;
    float3 centerWorld, adjacentWorld;
    if (!JE_SubsurfaceWorldPosition(depth, SSSView.PassParams.y, uv,
        SSSView.InvVP, centerWorld) ||
        !JE_SubsurfaceWorldPosition(depth, SSSView.PassParams.y,
            uv + float2(axis) * SSSView.SizeAndInvSize.zw, SSSView.InvVP, adjacentWorld))
        return source;
    const float worldPerPixel = length(adjacentWorld - centerWorld);
    if (!isfinite(worldPerPixel) || worldPerPixel <= 1.0e-12) return source;
    const float radiusWorld = min(SSSProfiles.RadiusWorld[profile].w * SSSView.PassParams.x, original.a);
    const float radiusPixels = min(radiusWorld / worldPerPixel, SSSView.FilterParams.w);
    if (!isfinite(radiusPixels) || radiusPixels < 0.5) return source;

    float3 normal = SSSNormal.Load(int3(pixel, 0)).rgb * 2.0 - 1.0;
    const float normalLengthSquared = dot(normal, normal);
    if (!all(isfinite(normal)) || normalLengthSquared < 1.0e-8) return source;
    normal *= rsqrt(normalLengthSquared);

    float3 filtered = 0.0;
    [unroll] for (uint tap = 0; tap < JE_SUBSURFACE_KERNEL_SAMPLES; ++tap)
    {
        const float4 kernel = SSSProfiles.Kernel[profile * JE_SUBSURFACE_KERNEL_SAMPLES + tap];
        const int2 neighbor = pixel + int2(round(float2(axis) * (kernel.w * radiusPixels)));
        float3 sampleColor = source.rgb;
        if (all(neighbor >= 0) && all(neighbor < int2(SSSView.SizeAndInvSize.xy)))
        {
            const float4 neighborMask = SSSMask.Load(int3(neighbor, 0));
            const float4 neighborOriginal = SSSOriginalDiffuse.Load(int3(neighbor, 0));
            uint neighborProfile;
            if (JE_SubsurfaceActive(neighborMask, neighborOriginal.a, neighborProfile) &&
                neighborProfile == profile)
            {
                float3 neighborNormal = SSSNormal.Load(int3(neighbor, 0)).rgb * 2.0 - 1.0;
                const float lengthSquared = dot(neighborNormal, neighborNormal);
                const float neighborDepth = SSSDepth.Load(int3(neighbor, 0)).r;
                const float2 neighborUV = (float2(neighbor) + 0.5) * SSSView.SizeAndInvSize.zw;
                float3 neighborWorld;
                if (lengthSquared > 1.0e-8 && all(isfinite(neighborNormal)) &&
                    JE_SubsurfaceWorldPosition(neighborDepth, SSSView.PassParams.y, neighborUV,
                        SSSView.InvVP, neighborWorld) &&
                    dot(normal, neighborNormal * rsqrt(lengthSquared)) >= SSSView.FilterParams.z &&
                    abs(dot(neighborWorld - centerWorld, normal)) <= SSSView.FilterParams.y)
                {
                    const float3 value = SSSInputDiffuse.Load(int3(neighbor, 0)).rgb;
                    if (all(isfinite(value)))
                        sampleColor = lerp(source.rgb, value, step(1.0e-6, neighborMask.rgb));
                }
            }
        }
        // Rejected/offscreen taps return their energy to this pixel. Constant
        // fields stay constant at silhouettes, depth edges and profile edges.
        filtered += kernel.rgb * sampleColor;
    }
    return float4(filtered, original.a);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadID.xy;
    if (any(pixel >= uint2(SSSView.SizeAndInvSize.xy))) return;
#if JE_SUBSURFACE_PASS == 0
    SSSResult[pixel] = JE_SubsurfaceFilter(int2(pixel), int2(1, 0));
#elif JE_SUBSURFACE_PASS == 1
    SSSResult[pixel] = JE_SubsurfaceFilter(int2(pixel), int2(0, 1));
#elif JE_SUBSURFACE_PASS == 2
    const float4 scene = SSSSceneColor.Load(int3(pixel, 0));
    const float4 original = SSSOriginalDiffuse.Load(int3(pixel, 0));
    const float4 mask = SSSMask.Load(int3(pixel, 0));
    uint profile;
    float4 result = scene;
    if (JE_SubsurfaceActive(mask, original.a, profile))
    {
        const float3 filtered = SSSInputDiffuse.Load(int3(pixel, 0)).rgb;
        if (all(isfinite(original.rgb)) && all(isfinite(filtered)))
            result.rgb = JE_SubsurfaceCompose(scene.rgb, original.rgb,
                filtered, mask.rgb, SSSView.FilterParams.x);
    }
    SSSResult[pixel] = result;
#else
#error JE_SUBSURFACE_PASS must be 0 (horizontal), 1 (vertical), or 2 (composition).
#endif
}
