float4 main(float4 position : SV_POSITION) : SV_Target0
{
    const float2 outputUv = position.xy * Capture.FitAndInvSize.zw;
    const float2 sourceUv = (outputUv - 0.5) * Capture.FitAndInvSize.xy + 0.5;
    if (any(sourceUv < 0.0) || any(sourceUv > 1.0))
        return float4(0.0, 0.0, 0.0, 1.0);
    return float4(CaptureSource.SampleLevel(CaptureSourceSampler, sourceUv, 0).rgb, 1.0);
}
