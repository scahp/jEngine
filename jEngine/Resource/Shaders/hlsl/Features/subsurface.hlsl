// Declaration-free screen-space diffusion helpers. Kernel/profile generation
// lives in Renderer/Features/jSubsurfaceDiffusion.cpp; IDs 0..7 are persistent.
#define JE_SUBSURFACE_PROFILE_COUNT 8u
#define JE_SUBSURFACE_KERNEL_SAMPLES 17u

bool JE_SubsurfaceProfile(float encodedProfile, out uint profile)
{
    profile = 0;
    if (!isfinite(encodedProfile) || encodedProfile < 0.5 || encodedProfile > 8.5)
        return false;
    const float rounded = floor(encodedProfile + 0.5);
    if (abs(rounded - encodedProfile) > 0.01) return false;
    profile = (uint)rounded - 1u;
    return profile < JE_SUBSURFACE_PROFILE_COUNT;
}

bool JE_SubsurfaceWorldPosition(float depth, float clearDepth, float2 uv,
    float4x4 inverseViewProjection, out float3 world)
{
    world = 0.0;
    if (!isfinite(depth) || depth < 0.0 || depth > 1.0 || abs(depth - clearDepth) < 1.0e-7)
        return false;
    const float4 homogeneous = mul(inverseViewProjection,
        float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
    if (!all(isfinite(homogeneous)) || abs(homogeneous.w) <= 1.0e-10) return false;
    world = homogeneous.xyz / homogeneous.w;
    return all(isfinite(world));
}

float3 JE_SubsurfaceCompose(float3 scene, float3 originalDiffuse,
    float3 filteredDiffuse, float3 mask, float strength)
{
    return scene + saturate(mask) * saturate(strength) * (filteredDiffuse - originalDiffuse);
}
