struct SurfelData
{
    float4 PositionRadius;
    float4 NormalSeenFrame;
    float4 AlbedoWeight;
    float4 Extra;
};

struct SurfelActiveCounter
{
    uint Count;
    uint3 Padding;
};

StructuredBuffer<SurfelData> SurfelPool : register(t0, space0);
RWStructuredBuffer<uint> ActiveIndexBuffer : register(u1, space0);
RWStructuredBuffer<SurfelActiveCounter> ActiveCounterBuffer : register(u2, space0);

cbuffer CompactParam : register(b3, space0)
{
    uint MaxSurfels;
    uint3 Padding;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint surfelIndex = DTid.x;
    if (surfelIndex >= max(MaxSurfels, 1u))
        return;

    const SurfelData surfel = SurfelPool[surfelIndex];
    if (surfel.Extra.y <= 0.5)
        return;

    uint outIndex = 0u;
    InterlockedAdd(ActiveCounterBuffer[0].Count, 1u, outIndex);
    ActiveIndexBuffer[outIndex] = surfelIndex;
}
