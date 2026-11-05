#include "common.hlsl"
// Advanced shadow shader injection point; declarations precede helper bodies.
#include "Features/advancedshadows.hlsl"
#ifndef JE_ADVANCED_SHADOW_VOLUME
#define JE_ADVANCED_SHADOW_VOLUME 0
#endif
#if !JE_ADVANCED_SHADOW_VOLUME
#include "Features/materialvertex.hlsl"
#endif

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    nointerpolation float Opacity : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;
#if JE_ADVANCED_SHADOW_VOLUME
    // Core supplies already transformed, closed world-space volume geometry.
    // This variant deliberately requires only the AdvancedShadow group.
    output.WorldPosition = input.Position;
    output.Opacity = 1.0;
    output.Pos = mul(AdvancedShadow.ShadowVP[0], float4(input.Position, 1.0));
#else
    float3 normal = float3(0, 1, 0), tangent = float3(1, 0, 0);
    JE_DeformWater(input.Position, normal, tangent);
    const float4 world = mul(RenderObjectParam.M, float4(input.Position, 1.0));
    output.WorldPosition = world.xyz;
    output.Opacity = RenderObjectParam.BaseColorOpacity.a;
    const uint drawIndex = min((uint)AdvancedShadow.ModeParams.w, 5u);
    if ((uint)AdvancedShadow.ModeParams.y == JE_SHADOW_PROJECTION_DUAL)
        output.Pos = JE_DualParaboloidClipPosition(world.xyz - AdvancedShadow.LightPositionAndNear.xyz,
            min(drawIndex, 1u), AdvancedShadow.LightPositionAndNear.w,
            AdvancedShadow.FarAndDeepParams.x, AdvancedShadow.AtlasParams.w);
    else
        output.Pos = mul(AdvancedShadow.ShadowVP[drawIndex], world);
#endif
    return output;
}
