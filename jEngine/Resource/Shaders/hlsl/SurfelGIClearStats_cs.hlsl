struct SurfelGIStats
{
    uint ActiveCount;
    uint DormantCount;
    uint MismatchCount;
    uint TTLRetireCount;
    uint PageGCCount;
    uint PageEvictCount;
    uint Padding0;
    uint Padding1;
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
    s.Padding0 = 0u;
    s.Padding1 = 0u;
    StatsBuffer[0] = s;
}
