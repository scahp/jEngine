
#ifndef SHOW_AO_ONLY
#define SHOW_AO_ONLY 0
#endif // SHOW_AO_ONLY

RWTexture2D<float4> resultImage : register(u0);
Texture2D inputImage : register(t1);

struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    int UseWaveIntrinsics;
    float AOIntensity;
};

cbuffer ComputeCommon : register(b2) { CommonComputeUniformBuffer ComputeCommon; }

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

#if SHOW_AO_ONLY
    resultImage[int2(GlobalInvocationID.xy)].xyz = lerp(float3(1.0f, 1.0f, 1.0f), inputImage[uint2(GlobalInvocationID.xy)].xyz, ComputeCommon.AOIntensity);
#else
    resultImage[int2(GlobalInvocationID.xy)].xyz *= lerp(float3(1.0f, 1.0f, 1.0f), inputImage[uint2(GlobalInvocationID.xy)].xyz, ComputeCommon.AOIntensity);
#endif
}