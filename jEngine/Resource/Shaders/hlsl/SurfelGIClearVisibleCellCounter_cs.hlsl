struct VisibleCellCounter
{
    uint Count;
    uint3 Padding;
};

RWStructuredBuffer<VisibleCellCounter> VisibleCellCounterBuffer : register(u0, space0);

[numthreads(1, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    (void)GlobalInvocationID;
    VisibleCellCounterBuffer[0].Count = 0u;
}
