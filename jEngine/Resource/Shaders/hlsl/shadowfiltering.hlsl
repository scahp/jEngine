#ifndef JENGINE_SHADOW_FILTERING_HLSL
#define JENGINE_SHADOW_FILTERING_HLSL

// Fixed variant of the ring-disk PCF samples in scahp/Shadows (MIT), master
// 87853997029283edfbbb60eaad03cf163c95f26c, Shaders/shadow.glsl.
// Bake 16 samples / 11 rings, radius pow((i + 1) / 16, .75), rotation zero.
// No frame jitter or screen-space seed: stable views have stable filtering.
static const float2 ShadowPoisson16[16] =
{
    float2( 0.125000000,  0.000000000),
    float2(-0.080449282, -0.194221747),
    float2(-0.201481862,  0.201481862),
    float2( 0.326640741,  0.135299025),
    float2( 0.000000000, -0.417962691),
    float2(-0.442729842,  0.183384705),
    float2( 0.380380763,  0.380380763),
    float2( 0.227544930, -0.549342057),
    float2(-0.649519053,  0.000000000),
    float2( 0.268998386,  0.649419551),
    float2( 0.533874932, -0.533874932),
    float2(-0.744579875, -0.308415082),
    float2( 0.000000000,  0.855790630),
    float2( 0.835836603, -0.346214857),
    float2(-0.673695210, -0.673695210),
    float2(-0.382683432,  0.923879533),
};

// Returns comparison visibility. The caller owns UV projection, reference-depth
// encoding, bias, reverse-Z inversion and whether the light casts any shadows.
float SampleShadowVisibility2D(Texture2D shadowMap, SamplerComparisonState comparison,
    float2 uv, float referenceDepth, float radiusTexels, int mode)
{
    [branch]
    if (mode != 1 || !isfinite(radiusTexels) || radiusTexels <= 0.0)
        return shadowMap.SampleCmpLevelZero(comparison, uv, referenceDepth);

    uint width, height;
    shadowMap.GetDimensions(width, height);
    // Forward uses a constant 1x1 neutral map for an absent light shadow. Its
    // border value can differ from its depth value, notably with reverse-Z.
    [branch]
    if (width <= 1 && height <= 1)
        return shadowMap.SampleCmpLevelZero(comparison, uv, referenceDepth);

    const float2 radiusUV = min(radiusTexels, 4.0) / float2(max(width, 1u), max(height, 1u));
    float visibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < 16; ++sampleIndex)
        visibility += shadowMap.SampleCmpLevelZero(comparison, uv + ShadowPoisson16[sampleIndex] * radiusUV, referenceDepth);
    return visibility * (1.0 / 16.0);
}

float SampleShadowVisibilityCube(TextureCube shadowMap, SamplerComparisonState comparison,
    float3 lightDirection, float referenceDepth, float radiusTexels, int mode)
{
    [branch]
    if (mode != 1 || !isfinite(radiusTexels) || radiusTexels <= 0.0)
        return shadowMap.SampleCmpLevelZero(comparison, lightDirection, referenceDepth);

    uint width, height;
    shadowMap.GetDimensions(width, height);
    const float lengthSquared = dot(lightDirection, lightDirection);
    [branch]
    if ((width <= 1 && height <= 1) || !all(isfinite(lightDirection))
        || !isfinite(lengthSquared) || lengthSquared <= 1e-12)
        return shadowMap.SampleCmpLevelZero(comparison, lightDirection, referenceDepth);

    const float3 direction = lightDirection * rsqrt(lengthSquared);
    const float3 axis = abs(direction.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    const float3 tangent = normalize(cross(axis, direction));
    const float3 bitangent = cross(direction, tangent);
    // A cube face spans two direction units. Offsets are angular footprints
    // around the lookup direction; hardware cube sampling handles face seams.
    const float radiusDirection = 2.0 * min(radiusTexels, 4.0) / float(max(width, 1u));
    float visibility = 0.0;
    [unroll]
    for (uint sampleIndex = 0; sampleIndex < 16; ++sampleIndex)
    {
        const float2 offset = ShadowPoisson16[sampleIndex] * radiusDirection;
        visibility += shadowMap.SampleCmpLevelZero(comparison,
            direction + tangent * offset.x + bitangent * offset.y, referenceDepth);
    }
    return visibility * (1.0 / 16.0);
}

#endif
