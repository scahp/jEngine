[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    (void)DTid;
    const uint activeSurfelCount = ActiveCounterBuffer[0];
    const uint groupCountX = max((activeSurfelCount + 63u) / 64u, 1u);

    jSurfelInlineRayDispatchArgsGPU args;
    args.GroupCountX = groupCountX;
    args.GroupCountY = 1u;
    args.GroupCountZ = 1u;
    args.Padding0 = 0u;
    DispatchArgsBuffer[0] = args;
}
