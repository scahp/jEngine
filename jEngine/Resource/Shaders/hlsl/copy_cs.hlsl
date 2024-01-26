Texture2D inputImage : register(t0);
RWTexture2D<float4> resultImage : register(u1);

struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    float UseWaveIntrinsics;
    float Padding;
};

cbuffer ComputeCommon : register(b2)
{
    CommonComputeUniformBuffer ComputeCommon;
}

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    resultImage[int2(GlobalInvocationID.xy)] = inputImage[uint2(GlobalInvocationID.xy)];
}
