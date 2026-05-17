[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint surfelIndex = DTid.x;
    if (surfelIndex >= max(ActiveCompactUniformBuffer.MaxSurfels, 1u))
        return;

    const jSurfelGPU surfel = SurfelPool[surfelIndex];
    if (surfel.Extra.y <= 0.5)
        return;

    uint outIndex = 0u;
    InterlockedAdd(ActiveCounterBuffer[0], 1u, outIndex);
    ActiveIndexBuffer[outIndex] = surfelIndex;
}
