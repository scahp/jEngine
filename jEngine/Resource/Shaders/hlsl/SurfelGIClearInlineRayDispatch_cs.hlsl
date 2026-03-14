[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    (void)DTid;
    ActiveCounterBuffer[0].Count = 0u;

    jSurfelInlineRayDispatchArgsGPU args;
    args.GroupCountX = 1u;
    args.GroupCountY = 1u;
    args.GroupCountZ = 1u;
    args.Padding0 = 0u;
    DispatchArgsBuffer[0] = args;
}
