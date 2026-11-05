#ifndef JENGINE_ADVANCED_SHADOW_LIGHTING_HLSL
#define JENGINE_ADVANCED_SHADOW_LIGHTING_HLSL 1

#include "Features/advancedshadows.hlsl"

// Lighting adapters accept the generated value block and explicit resources;
// none of the helpers captures a particular light or a mutable global binding.
float JE_SampleAdvancedShadowFace(jAdvancedShadowData data, Texture2D shadowMap,
    Texture2D deepShadow0, Texture2D deepShadow1, SamplerState filtering,
    float3 projected, uint face)
{
    return JE_SampleAdvancedShadowRect(shadowMap, deepShadow0, deepShadow1, filtering,
        projected, JE_ShadowAtlasRect(face, uint2(data.AtlasParams.xy)),
        (uint)data.ModeParams.x, data.FarAndDeepParams.y, data.FarAndDeepParams.z,
        data.FilterParams.x, data.FilterParams.y, data.FilterParams.z,
        data.SamplingParams.y, (int)data.SamplingParams.x, data.FarAndDeepParams.w);
}

float JE_SampleAdvancedLightShadow(jAdvancedShadowData data, Texture2D shadowMap,
    Texture2D deepShadow0, Texture2D deepShadow1, SamplerState filtering,
    float3 worldPosition, float4x4 mainViewVP)
{
    if (data.SamplingParams.z <= 0.5 || !all(isfinite(worldPosition))) return 1.0;
    const uint representation = (uint)data.ModeParams.x;
    const uint projection = (uint)data.ModeParams.y;
    if (representation == JE_SHADOW_DEEP)
    {
        const float viewDepth = dot(worldPosition - data.CameraPositionAndType.xyz,
            data.CameraForwardAndOpacity.xyz);
        if (!isfinite(viewDepth) || viewDepth < 0.0 || viewDepth > data.SamplingParams.w) return 1.0;
    }
    float3 projected;
    if (representation == JE_SHADOW_VOLUME)
    {
        // Closed-volume z-fail stencil has already resolved visibility in the
        // main camera. Light-space matrices do not apply to this screen mask.
        if (!JE_ProjectShadow(mainViewVP, worldPosition, projected)) return 1.0;
        const float2 uv = JE_ClampedShadowAtlasUV(shadowMap, projected.xy, float4(0, 0, 1, 1));
        return saturate(shadowMap.SampleLevel(filtering, uv, 0.0).r);
    }

    uint face = 0;
    if (projection == JE_SHADOW_PROJECTION_CSM)
    {
        const uint count = clamp((uint)data.ModeParams.z, 1u, 4u);
        const float viewDepth = dot(worldPosition - data.CameraPositionAndType.xyz,
            data.CameraForwardAndOpacity.xyz);
        if (!isfinite(viewDepth) || viewDepth < 0.0 || viewDepth > data.CascadeSplits[count - 1]) return 1.0;
        face = JE_SelectShadowCascade(data.CascadeSplits, count, viewDepth);
    }
    else if (projection == JE_SHADOW_PROJECTION_CUBE_ATLAS)
        face = JE_ShadowCubeFace(worldPosition - data.LightPositionAndNear.xyz);
    else if (projection == JE_SHADOW_PROJECTION_DUAL)
    {
        const float3 relative = worldPosition - data.LightPositionAndNear.xyz;
        const float radius = length(relative);
        if (!isfinite(radius) || radius <= data.LightPositionAndNear.w || radius > data.FarAndDeepParams.x)
            return 1.0;
        face = relative.z >= 0.0 ? 0u : 1u;
        const float4 clipPosition = JE_DualParaboloidClipPosition(relative, face,
            data.LightPositionAndNear.w, data.FarAndDeepParams.x, data.AtlasParams.w);
        projected = float3(clipPosition.x * 0.5 + 0.5, 0.5 - clipPosition.y * 0.5, clipPosition.z);
        float visibility = JE_SampleAdvancedShadowFace(data, shadowMap, deepShadow0,
            deepShadow1, filtering, projected, face);
        if (abs(JE_DualParaboloidDirection(relative).z) <= JE_DualParaboloidSeam(data.AtlasParams.w))
        {
            const uint other = 1u - face;
            const float4 otherClip = JE_DualParaboloidClipPosition(relative, other,
                data.LightPositionAndNear.w, data.FarAndDeepParams.x, data.AtlasParams.w);
            projected = float3(otherClip.x * 0.5 + 0.5, 0.5 - otherClip.y * 0.5, otherClip.z);
            visibility = min(visibility, JE_SampleAdvancedShadowFace(data, shadowMap,
                deepShadow0, deepShadow1, filtering, projected, other));
        }
        return visibility;
    }

    // Single/PSM use index zero; CSM/cube use the chosen exact raster matrix.
    if (!JE_ProjectShadow(data.ShadowVP[face], worldPosition, projected)) return 1.0;
    return JE_SampleAdvancedShadowFace(data, shadowMap, deepShadow0, deepShadow1,
        filtering, projected, face);
}

#endif
