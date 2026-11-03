// Immutable input for every multi-bounce lookup in the following gather dispatch.
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint count, stride;
    SourceIrradiance.GetDimensions(count, stride);
    if (DTid.x < count)
        PreviousIrradiance[DTid.x] = SourceIrradiance[DTid.x];
}
