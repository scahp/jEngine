float4 main(float4 position : SV_POSITION) : SV_TARGET0
{
    const int2 pixel = int2(position.xy);
    uint width, height, samples;
    DepthMS.GetDimensions(width, height, samples);
    float nearestDepth = 1.0;
    uint foreground = 0;
    for (uint sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        const float depth = DepthMS.Load(pixel, sampleIndex);
        if (depth < nearestDepth) { nearestDepth = depth; foreground = sampleIndex; }
    }
    if (nearestDepth >= 1.0) return 0.0;
    const float4 selected = SkinUVMS.Load(pixel, foreground);
    // UV lighting is an unweighted surface value, whereas the resolved screen
    // diffuse is a coverage-weighted mean. Keep the screen result at partial
    // coverage and mixed-surface pixels rather than inventing a full surface
    // or replacing another object's diffuse contribution.
    for (uint sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
        if (DepthMS.Load(pixel, sampleIndex) >= 1.0 ||
            any(SkinUVMS.Load(pixel, sampleIndex).ba != selected.ba)) return 0.0;
    // Same nearest-depth foreground metadata as the main eight-target resolve.
    return selected;
}
