[numthreads(1, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    (void)GlobalInvocationID;
    VisibleCellCounterBuffer[0].Count = 0u;
}
