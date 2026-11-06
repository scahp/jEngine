[numthreads(16, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (Probe.Options.x < 0)
    {
        if (id.x < 16) ProbeOutput[uint2(id.x, 0)] = 0.0;
        return;
    }
    if (id.x != 0) return;
    uint width, height;
    PixelSource.GetDimensions(width, height);
    uint2 pixel = min((uint2)(saturate(Probe.Location.xy) * float2(width, height)), uint2(width - 1, height - 1));
    float4 value = PixelSource.Load(int3(pixel, 0));
    ProbeOutput[uint2((uint)Probe.Options.x, 0)] = value;
    if ((int)Probe.Options.x == 4)
    {
        float2 uv = (float2(pixel) + 0.5) / float2(width, height);
        float4 world = mul(Probe.InvVP, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, value.r, 1.0));
        bool valid = value.r < 1.0 && abs(world.w) > 1e-8;
        ProbeOutput[uint2(15, 0)] = valid ? float4(world.xyz / world.w, 1.0) : 0.0;
    }
}
