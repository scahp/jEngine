[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint idx = DTid.x;
    if (idx < ClearParam.CandidateCapacity)
    {
        jSurfelCandidateGPU c;
        c.Surfel.PositionRadius = float4(0.0, 0.0, 0.0, 0.0);
        c.Surfel.Normal = float3(0.0, 0.0, 0.0);
        c.Surfel.LastSeenFrame = 0u;
        c.Surfel.AlbedoWeight = float4(0.0, 0.0, 0.0, 0.0);
        c.Surfel.State = 0;
        c.Surfel.IsActive = 0u;
        c.Surfel.OwnerCellHash = 0u;
        c.Surfel.CascadeIndex = 0u;
        c.Priority = 0u;
        c.Padding0 = 0u;
        c.Padding1 = 0u;
        c.Padding2 = 0u;
        CandidateBuffer[idx] = c;
    }

    if (idx < ClearParam.PageCapacity)
    {
        WinnerScoreBuffer[idx] = 0u;
        WinnerIndexBuffer[idx] = 0xffffffffu;
        WinnerLockBuffer[idx] = 0u;
    }
}
