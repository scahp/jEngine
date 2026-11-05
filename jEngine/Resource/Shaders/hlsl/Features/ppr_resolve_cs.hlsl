#include "common.hlsl"
#include "Features/pixelprojectedreflection.hlsl"

#ifndef PPR_USE_CUBE
#define PPR_USE_CUBE 0
#endif

bool PPRSourcePixel(int2 receiver, int2 projectionPixel, int2 size, out int2 source)
{
    source = 0;
    if (any(projectionPixel < 0) || any(projectionPixel >= size)) return false;
    const uint packed = Projection[projectionPixel];
    if (packed == PPR_EMPTY) return false;
    source = receiver - PPRDecodeOffset(packed);
    return all(source >= 0) && all(source < size);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 p = dispatchThreadID.xy;
    const int2 size = int2(FeatureView.SizeAndInvSize.xy);
    if (any(p >= uint2(size))) return;
    const float4 scene = SceneColor.Load(int3(p, 0));
    const float depth = SceneDepth.Load(int3(p, 0)).r;
    if (depth >= 0.999999) { Result[p] = scene; return; }
    const float2 uv = (float2(p) + 0.5) * FeatureView.SizeAndInvSize.zw;
    const float3 position = CalcWorldPositionFromDepth(depth, uv, FeatureView.InvVP);
    const float3 unpackedNormal = SceneNormal.Load(int3(p, 0)).rgb * 2.0 - 1.0;
    const float3 normal = unpackedNormal * rsqrt(max(dot(unpackedNormal, unpackedNormal), 1.0e-12));
    const float2 material = saturate(SceneMaterial.Load(int3(p, 0)).zw);
    const float planeDistance = abs(dot(Reflection.Plane.xyz, position) - Reflection.Plane.w);
    if (!all(isfinite(position)) || planeDistance > Reflection.MaskAndIntensity.x ||
        abs(dot(normal, Reflection.Plane.xyz)) < Reflection.MaskAndIntensity.y ||
        material.y > Reflection.MaskAndIntensity.z)
    { Result[p] = scene; return; }

    float3 toEye = FeatureView.EyeWorld.w > 0.5 ? FeatureView.EyeWorld.xyz - position :
        CalcWorldPositionFromDepth(0.0, uv, FeatureView.InvVP) -
        CalcWorldPositionFromDepth(0.5, uv, FeatureView.InvVP);
    toEye *= rsqrt(max(dot(toEye, toEye), 1.0e-12));
    const float3 reflectedDirection = reflect(-toEye, normal);
    float3 reflected = Reflection.FallbackRadianceAndUseCube.rgb;
#if PPR_USE_CUBE
    if (Reflection.FallbackRadianceAndUseCube.w > 0.5)
        reflected = FallbackEnvironment.SampleLevel(FallbackEnvironmentSampler, reflectedDirection,
            material.y * Reflection.ResolveParams.z).rgb;
#endif

    int2 source;
    bool found = PPRSourcePixel(int2(p), int2(p), size, source);
    // A bounded 3x3 neighborhood fills small projection holes. Unrepresented or
    // off-screen geometry always retains the environment fallback above.
    if (!found && Reflection.ResolveParams.x > 0.5)
    {
        [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            int2 candidate;
            if (!found && PPRSourcePixel(int2(p), int2(p) + int2(x, y), size, candidate))
            { source = candidate; found = true; }
        }
    }
    if (found)
    {
        const float2 sourceUV = (float2(source) + 0.5) * FeatureView.SizeAndInvSize.zw;
        const float2 edge = min(sourceUV, 1.0 - sourceUV);
        const float edgeConfidence = Reflection.ResolveParams.y > 0.0 ?
            saturate(min(edge.x, edge.y) / Reflection.ResolveParams.y) : 1.0;
        reflected = lerp(reflected, SceneColor.Load(int3(source, 0)).rgb, edgeConfidence);
    }
    const float3 albedo = saturate(SceneAlbedo.Load(int3(p, 0)).rgb);
    const float3 f0 = lerp(0.04.xxx, albedo, material.x);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(abs(dot(normal, toEye))), 5.0);
    const float smoothness = 1.0 - material.y;
    // The feature chain's storage is RGBA16F. Keep accepted high-radiance
    // authoring values representable before the ordinary scene tonemapper.
    Result[p] = float4(clamp(scene.rgb + max(0.0, reflected) * fresnel *
        (smoothness * smoothness * Reflection.MaskAndIntensity.w), 0.0, 65000.0), scene.a);
}
