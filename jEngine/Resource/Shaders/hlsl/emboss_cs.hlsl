Texture2D inputImage : register(t0);
RWTexture2D<float4> resultImage : register(u1);

float conv(in float kernel[9], in float data[9], in float denom, in float offset)
{
    float res = 0.0;
    for (int i = 0; i < 9; ++i)
    {
        res += kernel[i] * data[i];
    }
    return saturate(res / denom + offset);
}

struct CommonComputeUniformBuffer
{
    float Width;
    float Height;
    float UseWaveIntrinsics;
    float Padding;
};

cbuffer ComputeCommon : register(b2) { CommonComputeUniformBuffer ComputeCommon; }

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    float3 rgb = inputImage[uint2(GlobalInvocationID.xy)].rgb;
    
    // Wave operation test
    if (ComputeCommon.UseWaveIntrinsics <= 0)
    {
        resultImage[int2(GlobalInvocationID.xy)] = float4(rgb, 1.0);
    }
    else
    {
        uint4 activeLaneMask = WaveActiveBallot(true);
        uint numActiveLanes = countbits(activeLaneMask.x) + countbits(activeLaneMask.y) + countbits(activeLaneMask.z) + countbits(activeLaneMask.w);
        float4 avgColor = float4(WaveActiveSum(rgb) / float(numActiveLanes), 1.0);
        resultImage[int2(GlobalInvocationID.xy)] = avgColor;
    }
}
