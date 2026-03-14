[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    resultImage[int2(GlobalInvocationID.xy)] = inputImage[uint2(GlobalInvocationID.xy)];
}
