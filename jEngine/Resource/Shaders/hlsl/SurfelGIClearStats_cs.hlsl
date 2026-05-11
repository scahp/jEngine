[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    (void)DTid;
    jSurfelGIStatsGPU s;
    s.ActiveCount = 0u;
    s.MismatchCount = 0u;
    s.ReservoirOverflowCount = 0u;
    s.ReservoirRejectedCount = 0u;
    StatsBuffer[0] = s;
}
