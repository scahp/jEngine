#ifndef JENGINE_MATERIAL_SURFACE_ADAPTER
#define JENGINE_MATERIAL_SURFACE_ADAPTER 1

#ifndef JE_HAS_MATERIAL_GRAPH
#define JE_HAS_MATERIAL_GRAPH 0
#endif
#ifndef JE_HAS_HORIZON
#define JE_HAS_HORIZON 0
#endif

#if JE_HAS_MATERIAL_GRAPH
// Generated graph output is the material default. Instance override bits then
// select native textures/scalars, with final instance RGB taking precedence over
// a texture override. Both raster renderers use this same ordering.
JEMaterialSurface JE_ResolveMaterial(JEMaterialContext ctx, bool nativeDiffuseSRGB)
{
    JEMaterialSurface surface = JE_EvaluateMaterial(ctx);
    const uint mask = MaterialGraphParam.OutputOverridesMask;
    if ((mask & 16u) != 0u)
    {
        float4 textureColor = DiffuseTexture.Sample(DiffuseTextureSampler, ctx.UV);
        if (nativeDiffuseSRGB) textureColor.rgb = pow(max(textureColor.rgb, 0.0), 2.2);
        surface.BaseColor.rgb = textureColor.rgb * RenderObjectParam.BaseColorOpacity.rgb;
        surface.BaseColor.a = textureColor.a;
    }
    if ((mask & 1u) != 0u)
        surface.BaseColor.rgb = RenderObjectParam.BaseColorOpacity.rgb;
    // Graph surface opacity is stored only in the object block. Generated alpha
    // contains texture alpha, so opacity is applied exactly once here.
    surface.BaseColor.a *= RenderObjectParam.BaseColorOpacity.a;
    if ((mask & 2u) != 0u)
        surface.NormalTS = JE_DecodeNormalTS(NormalTexture.Sample(NormalTextureSampler, ctx.UV).rgb, 1.0);
    if ((mask & 4u) != 0u) surface.Metallic = saturate(RenderObjectParam.Metallic);
    if ((mask & 8u) != 0u) surface.Roughness = saturate(RenderObjectParam.Roughness);
    return surface;
}
#endif

#endif
