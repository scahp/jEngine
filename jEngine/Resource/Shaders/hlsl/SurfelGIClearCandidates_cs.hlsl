struct SurfelData
{
    float4 PositionRadius;
    float4 NormalSeenFrame;
    float4 AlbedoWeight;
    float4 Extra;
};

struct SurfelCandidate
{
    SurfelData Surfel;
    int4 CellCascade;
    uint Priority;
    uint3 Padding;
};

RWStructuredBuffer<SurfelCandidate> CandidateBuffer : register(u0, space0);
RWStructuredBuffer<uint> WinnerScoreBuffer : register(u1, space0);
RWStructuredBuffer<uint> WinnerIndexBuffer : register(u2, space0);
RWStructuredBuffer<uint> WinnerLockBuffer : register(u3, space0);

cbuffer ClearParam : register(b4, space0)
{
    uint CandidateCapacity;
    uint PageCapacity;
    uint2 Padding;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint idx = DTid.x;
    if (idx < CandidateCapacity)
    {
        SurfelCandidate c;
        c.Surfel.PositionRadius = float4(0.0, 0.0, 0.0, 0.0);
        c.Surfel.NormalSeenFrame = float4(0.0, 0.0, 0.0, 0.0);
        c.Surfel.AlbedoWeight = float4(0.0, 0.0, 0.0, 0.0);
        c.Surfel.Extra = float4(0.0, 0.0, 0.0, 0.0);
        c.CellCascade = int4(0, 0, 0, 0);
        c.Priority = 0u;
        c.Padding = uint3(0u, 0u, 0u);
        CandidateBuffer[idx] = c;
    }

    if (idx < PageCapacity)
    {
        WinnerScoreBuffer[idx] = 0u;
        WinnerIndexBuffer[idx] = 0xffffffffu;
        WinnerLockBuffer[idx] = 0u;
    }
}
