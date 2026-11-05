// Pixel-projected planar reflection: scatter reflected visible scene geometry
// into an intermediate image, then gather scene radiance in the resolve pass.
// Adapted from scahp/Shadows origin/sponza (MIT), without its OpenGL ownership.
#include "common.hlsl"
#include "Features/pixelprojectedreflection.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 p = dispatchThreadID.xy;
    const uint2 size = uint2(FeatureView.SizeAndInvSize.xy);
    if (any(p >= size)) return;
    const float depth = SceneDepth.Load(int3(p, 0)).r;
    if (depth >= 0.999999) return;
    const float2 uv = (float2(p) + 0.5) * FeatureView.SizeAndInvSize.zw;
    const float3 position = CalcWorldPositionFromDepth(depth, uv, FeatureView.InvVP);
    const float signedDistance = dot(Reflection.Plane.xyz, position) - Reflection.Plane.w;
    if (!all(isfinite(position)) || signedDistance <= Reflection.MaskAndIntensity.x) return;
    const float3 mirrored = position - 2.0 * signedDistance * Reflection.Plane.xyz;
    const float4 clip = mul(FeatureView.VP, float4(mirrored, 1.0));
    if (!all(isfinite(clip)) || clip.w <= 1.0e-6) return;
    const float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) >= 1.0) || ndc.z < 0.0 || ndc.z > 1.0) return;
    const float2 targetUV = ndc.xy * float2(0.5, -0.5) + 0.5;
    const uint2 target = uint2(targetUV * float2(size));
    if (any(target >= size)) return;
    const uint packed = PPREncodeOffset(int2(target) - int2(p));
    if (packed != PPR_EMPTY)
        InterlockedMin(Projection[target], packed);
}
