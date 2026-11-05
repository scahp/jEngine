// Test-only conversion of actual production MSAA images for native readback.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height, samples;
    ColorMS.GetDimensions(width, height, samples);
    if (id.x >= width || id.y >= height * samples) return;
    const int2 source = int2(id.x, id.y / samples);
    const uint sampleIndex = id.y % samples;
    SampleColors[id.xy] = ColorMS.Load(source, sampleIndex);
    SampleSurfaces[id.xy] = float4(NormalMS.Load(source, sampleIndex).rgb, DepthMS.Load(source, sampleIndex));
    if (id.y < height)
    {
        ResolvedColors[id.xy] = ResolvedColor.Load(int3(id.xy, 0));
        ResolvedSurfaces[id.xy] = float4(ResolvedNormal.Load(int3(id.xy, 0)).rgb,
            ResolvedDepth.Load(int3(id.xy, 0)).r);
    }
}
