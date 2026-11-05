// Production typed shader declarations are injected before this entry.
struct ResolveOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 Albedo : SV_TARGET2;
    float4 Material : SV_TARGET3;
    float4 Emission : SV_TARGET4;
    float4 Visibility : SV_TARGET5;
#if USE_SUBSURFACE
    float4 Mask : SV_TARGET6;
    float4 Diffuse : SV_TARGET7;
#endif
    float Depth : SV_DEPTH;
};

ResolveOutput main(float4 position : SV_POSITION)
{
    uint width, height, samples;
    SceneColorMS.GetDimensions(width, height, samples);
    const int2 pixel = int2(position.xy);
    ResolveOutput result = (ResolveOutput)0;
    uint foregroundSample = 0, foregroundCount = 0;
    float nearestDepth = 1.0;
    for (uint s = 0; s < samples; ++s)
    {
        result.Color += SceneColorMS.Load(pixel, s) / samples;
        result.Emission += EmissionMS.Load(pixel, s) / samples;
        const float depth = DepthMS.Load(pixel, s);
        if (depth < 1.0) ++foregroundCount;
        if (depth < nearestDepth) { nearestDepth = depth; foregroundSample = s; }
#if USE_SUBSURFACE
        result.Diffuse.rgb += DiffuseMS.Load(pixel, s).rgb / samples;
#endif
    }
    // Metadata must be from a single surface; averaging normals/profile IDs
    // across a silhouette invents surfaces and causes diffusion across edges.
    result.Normal = NormalMS.Load(pixel, foregroundSample);
    result.Albedo = AlbedoMS.Load(pixel, foregroundSample);
    result.Material = MaterialMS.Load(pixel, foregroundSample);
    result.Visibility = VisibilityMS.Load(pixel, foregroundSample);
    result.Depth = nearestDepth;
#if USE_SUBSURFACE
    result.Mask = SubsurfaceMaskMS.Load(pixel, foregroundSample);
    result.Mask.rgb *= (float)foregroundCount / samples;
    if (foregroundCount == 0) result.Mask = 0.0;
    result.Diffuse.a = DiffuseMS.Load(pixel, foregroundSample).a;
#endif
    return result;
}
