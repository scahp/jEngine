#include "common.hlsl"
#include "Features/atmosphere.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 p = dispatchThreadID.xy;
    if (any(p >= uint2(FeatureView.SizeAndInvSize.xy))) return;
    const float4 scene = SceneColor.Load(int3(p, 0));
    const float depth = SceneDepth.Load(int3(p, 0)).r;
    const bool sky = depth >= 0.999999;
    const float2 uv = (float2(p) + 0.5) * FeatureView.SizeAndInvSize.zw;
    // A finite far-plane point supplies the sky direction without introducing
    // the main camera's clip distance as an atmospheric integration distance.
    const float3 position = CalcWorldPositionFromDepth(sky ? 0.99999 : depth, uv, FeatureView.InvVP);
    const float3 rayOrigin = FeatureView.EyeWorld.w > 0.5 ? FeatureView.EyeWorld.xyz :
        CalcWorldPositionFromDepth(0.0, uv, FeatureView.InvVP);
    const float3 cameraToPoint = position - rayOrigin;
    const float lengthSquared = dot(cameraToPoint, cameraToPoint);
    if (!all(isfinite(position)) || lengthSquared <= 1.0e-12)
    { Result[p] = scene; return; }
    const float3 direction = cameraToPoint * rsqrt(lengthSquared);
    const float metersPerUnit = Atmosphere.DistanceParams.x;
    const float maxDistance = Atmosphere.DistanceParams.w;
    const float distance = sky ? maxDistance : min(maxDistance, sqrt(lengthSquared) * metersPerUnit);
    const float cameraHeight = max(0.0,
        (rayOrigin.y - Atmosphere.DistanceParams.y) * metersPerUnit);
    const float intensity = max(0.0, Atmosphere.SolarIrradianceAndIntensity.w);
    float3 transmittance;
    float3 scattering;
    IntegrateGroundAtmosphere(cameraHeight, direction, distance,
        Atmosphere.DistanceParams.z, maxDistance,
        Atmosphere.RayleighAndScaleHeight.xyz * intensity,
        Atmosphere.MieAndScaleHeight.xyz * intensity,
        float2(Atmosphere.RayleighAndScaleHeight.w, Atmosphere.MieAndScaleHeight.w),
        Atmosphere.SunDirectionAndG.xyz, Atmosphere.SunDirectionAndG.w,
        Atmosphere.SolarIrradianceAndIntensity.rgb,
        clamp(uint(Atmosphere.SampleCounts.x), 4u, 64u),
        clamp(uint(Atmosphere.SampleCounts.y), 1u, 16u), transmittance, scattering);
    Result[p] = float4(clamp(scene.rgb * transmittance + scattering, 0.0, 65000.0), scene.a);
}
