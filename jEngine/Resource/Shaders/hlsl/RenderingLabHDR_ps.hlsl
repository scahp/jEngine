float4 main(float4 position : SV_POSITION) : SV_Target0
{
    return LabSource.SampleLevel(LabSourceSampler, position.xy * LabCapture.InvSize.xy, 0);
}
