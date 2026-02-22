struct SurfelGIStats
{
    uint ActiveCount;
    uint DormantCount;
    uint MismatchCount;
    uint TTLRetireCount;
    uint PageGCCount;
    uint PageEvictCount;
    uint ReservoirOverflowCount;
    uint ReservoirRejectedCount;
};

RWStructuredBuffer<SurfelGIStats> StatsBuffer : register(u0, space0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    (void)DTid;
    SurfelGIStats s;
    s.ActiveCount = 0u;
    s.DormantCount = 0u;
    s.MismatchCount = 0u;
    s.TTLRetireCount = 0u;
    s.PageGCCount = 0u;
    s.PageEvictCount = 0u;
    s.ReservoirOverflowCount = 0u;
    s.ReservoirRejectedCount = 0u;
    StatsBuffer[0] = s;
}
