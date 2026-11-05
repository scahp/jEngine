#include "common.hlsl"
// Advanced shadow shader injection point; declarations precede helper bodies.
#include "Features/advancedshadows.hlsl"

#ifndef JE_ADVANCED_SHADOW_DEEP
#define JE_ADVANCED_SHADOW_DEEP 0
#endif
#ifndef JE_ADVANCED_SHADOW_VOLUME
#define JE_ADVANCED_SHADOW_VOLUME 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    nointerpolation float Opacity : TEXCOORD1;
};

float JE_AdvancedShadowFragmentDepth(VSOutput input)
{
    float depth = input.Pos.z;
    if ((uint)AdvancedShadow.ModeParams.y == JE_SHADOW_PROJECTION_DUAL)
    {
        const float3 relative = input.WorldPosition - AdvancedShadow.LightPositionAndNear.xyz;
        const float radius = length(relative);
        const float signZ = AdvancedShadow.ModeParams.w < 0.5 ? 1.0 : -1.0;
        const float3 direction = JE_DualParaboloidDirection(relative);
        clip(signZ * direction.z + JE_DualParaboloidSeam(AdvancedShadow.AtlasParams.w));
        depth = (radius - AdvancedShadow.LightPositionAndNear.w)
            / max(AdvancedShadow.FarAndDeepParams.x - AdvancedShadow.LightPositionAndNear.w, 1.0e-6);
    }
    if (!isfinite(depth)) discard;
    const uint projection = (uint)AdvancedShadow.ModeParams.y;
    if (projection == JE_SHADOW_PROJECTION_CSM || projection == JE_SHADOW_PROJECTION_PSM)
        return saturate(depth);
    if (depth < 0.0 || depth > 1.0) discard;
    return depth;
}

#if JE_ADVANCED_SHADOW_VOLUME
// Keep closed far caps and sides intact until the fixed-function depth test
// clamps them. Fragment discard or depth replacement would reopen the volume.
void main(VSOutput input)
{
}
#elif JE_ADVANCED_SHADOW_DEEP
struct DeepShadowOutput
{
    float4 Bins0 : SV_TARGET0;
    float4 Bins1 : SV_TARGET1;
};

// Both attachments clear to zero and blend ONE + ONE, including alpha. Disable
// depth testing/writes so every layer contributes; no stochastic alpha discard.
DeepShadowOutput main(VSOutput input)
{
    const float depth = JE_AdvancedShadowFragmentDepth(input);
    const float opacity = saturate(input.Opacity * max(AdvancedShadow.CameraForwardAndOpacity.w, 0.0));
    if (!isfinite(opacity) || opacity <= 0.0) discard;
    DeepShadowOutput output;
    JE_EncodeDeepShadowBins(depth, opacity, 16.0, output.Bins0, output.Bins1);
    return output;
}
#else
float main(VSOutput input) : SV_Depth
{
    const float depth = JE_AdvancedShadowFragmentDepth(input);
    const float opacity = saturate(input.Opacity);
    // Preserve the existing stable authored-opacity coverage for depth maps.
    if (!isfinite(opacity) || opacity <= frac(dot(input.Pos.xy, float2(0.754877666, 0.569840296)))) discard;
    return depth;
}
#endif
