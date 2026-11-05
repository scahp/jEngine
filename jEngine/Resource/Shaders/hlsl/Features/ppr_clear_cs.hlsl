[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 p = dispatchThreadID.xy;
    if (any(p >= uint2(FeatureView.SizeAndInvSize.xy))) return;
    Projection[p] = 0xffffffffu;
}
