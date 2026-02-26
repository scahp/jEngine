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

RWStructuredBuffer<SurfelActiveCounter> ActiveCounterBuffer : register(u0, space0);
RWStructuredBuffer<DispatchIndirectArgs> DispatchArgsBuffer : register(u1, space0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    (void)DTid;
    ActiveCounterBuffer[0].Count = 0u;

    DispatchIndirectArgs args;
    args.GroupCountX = 1u;
    args.GroupCountY = 1u;
    args.GroupCountZ = 1u;
    args.Padding = 0u;
    DispatchArgsBuffer[0] = args;
}
