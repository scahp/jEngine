float4 main(float4 position : SV_POSITION) : SV_Target0
{
    float2 uv = position.xy * Inspector.Display.zw;
    float4 value = InspectSource.SampleLevel(InspectSourceSampler, uv, 0);
    if (any(isnan(value)) || any(isinf(value))) return float4(1, 0, 1, 1);
    int mode = (int)Inspector.Options.x;
    float3 rgb = value.rgb;
    if (mode == 4 || mode == 5)
        rgb = mode == 4 ? value.xxx : saturate(abs(value.xxx) / max(Inspector.Options.z, 0.001));
    else if (mode == 6) rgb = value.zzz;
    else if (mode == 7) rgb = value.www;
    else if (mode == 10) rgb = value.xxx;
    else if (mode == 1 || mode == 8 || mode == 9 || mode == 11 || mode == 13)
    {
        rgb = max(rgb, 0.0) * exp2(Inspector.Options.y);
        rgb = pow(rgb / (1.0 + rgb), 1.0 / 2.2);
    }
    else if (mode == 2 || mode == 12) rgb = pow(max(rgb, 0.0), 1.0 / 2.2);
    // Normal is the encoded world-space normal used by post processing.
    return float4(saturate(rgb), 1.0);
}
