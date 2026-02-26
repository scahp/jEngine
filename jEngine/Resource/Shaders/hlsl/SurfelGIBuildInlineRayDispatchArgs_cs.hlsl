struct SurfelActiveCounter
{
    uint Count;
    uint3 Padding;
};

struct DispatchIndirectArgs
{
    uint GroupCountX;
    uint GroupCountY;
    uint GroupCountZ;
    uint Padding;
};

StructuredBuffer<SurfelActiveCounter> ActiveCounterBuffer : register(t0, space0);
RWStructuredBuffer<DispatchIndirectArgs> DispatchArgsBuffer : register(u1, space0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    (void)DTid;
    const uint activeSurfelCount = ActiveCounterBuffer[0].Count;
    const uint groupCountX = max((activeSurfelCount + 63u) / 64u, 1u);

    DispatchIndirectArgs args;
    args.GroupCountX = groupCountX;
    args.GroupCountY = 1u;
    args.GroupCountZ = 1u;
    args.Padding = 0u;
    DispatchArgsBuffer[0] = args;
}
