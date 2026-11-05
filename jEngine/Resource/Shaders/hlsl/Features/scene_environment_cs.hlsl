#include "common.hlsl"
#include "Features/environmentlighting.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 p = dispatchThreadID.xy;
    const uint2 size = uint2(FeatureView.SizeAndInvSize.xy);
    if (any(p >= size)) return;
    const float4 scene = SceneColor.Load(int3(p, 0));
    const float depth = SceneDepth.Load(int3(p, 0)).r;
    float3 indirectDiffuse = 0.0;
    if (depth < 0.999999)
    {
        // The current GBuffer producer stores world normal as RGB*0.5+0.5;
        // its historical OctaNormal comment does not describe the active path.
        const float3 unpackedNormal = SceneNormal.Load(int3(p, 0)).rgb * 2.0 - 1.0;
        const float3 normal = unpackedNormal * rsqrt(max(dot(unpackedNormal, unpackedNormal), 1.0e-12));
        const float3 albedo = SceneAlbedo.Load(int3(p, 0)).rgb;
        const float metallic = SceneMaterial.Load(int3(p, 0)).z;
        const float ambientAO = saturate(SceneAmbientOcclusion.Load(int3(p, 0)).y);
        indirectDiffuse = EvaluateSHDiffuse(Environment.RadianceSH, normal, albedo,
            metallic, Environment.IntensityAndPadding.x) * ambientAO;
    }
    Result[p] = float4(clamp(scene.rgb + indirectDiffuse, 0.0, 65000.0), scene.a);
#if JE_ENVIRONMENT_SUBSURFACE
    const float4 diffuse = SubsurfaceDiffuse.Load(int3(p, 0));
    SubsurfaceDiffuseResult[p] = float4(clamp(diffuse.rgb + indirectDiffuse, 0.0, 65000.0), diffuse.a);
#endif
}
