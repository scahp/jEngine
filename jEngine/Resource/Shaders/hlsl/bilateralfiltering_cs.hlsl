#include "common.hlsl"

#ifndef USE_GAUSSIAN_INSTEAD
#define USE_GAUSSIAN_INSTEAD 0
#endif // USE_GAUSSIAN_INSTEAD

RWTexture2D<float4> resultImage : register(u0);
Texture2D inputImage : register(t1);
Texture2D DepthBuffer : register(t2);
SamplerState DepthSampler : register(s2);

struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    float Sigma;
    int KernalSize;
    float SigmaForBilateral;
    float3 Padding0;            // for 16 byte alignment
};

cbuffer ComputeCommon : register(b3)
{
    CommonComputeUniformBuffer ComputeCommon;
}

struct jKernel
{
    float4 Data[150];
};
cbuffer KernelBuffer : register(b4) { jKernel Kernal; }

float Gaussian1D(float x, float sigma)
{
    return exp(-(x * x) / (2 * sigma * sigma)) / (sqrt(2 * PI) * sigma);
}

float Gaussian2D(float DistSq, float sigma)
{
    return exp(-0.5f * DistSq / (sigma * sigma)) / (2 * PI * sigma * sigma);
}

float GetIntensity(float3 LinearColor)
{
    return (LinearColor.x + LinearColor.y + LinearColor.z) / 3.0f;
}

float GetGaussian2DKernel(int x, int y)
{
    int LinearIndex = x + y * ComputeCommon.KernalSize;
    return Kernal.Data[LinearIndex / 4][LinearIndex % 4];
}

[numthreads(16, 16, 1)]
void Bilateral(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int kernelSize = ComputeCommon.KernalSize;
    int center = kernelSize / 2;
    int2 PixelPos = int2(GlobalInvocationID.xy);
    float2 CenterUV = PixelPos / float2(ComputeCommon.Width, ComputeCommon.Height);
    float CenterDepth = DepthBuffer.SampleLevel(DepthSampler, CenterUV, 0).x;
    
    float3 Color = float3(0, 0, 0);
    float TotalWeight = 0;
    for (int i = 0; i < kernelSize; ++i)
    {
        for (int j = 0; j < kernelSize; ++j)
        {
            int x = (i - center);
            int y = (j - center);
            //float Gs = Gaussian2D(x * x + y * y, ComputeCommon.Sigma);
            float Gs = GetGaussian2DKernel(i, j);
            
            float2 CurPixelPos = clamp(PixelPos + float2(x, y), float2(0, 0), float2(ComputeCommon.Width, ComputeCommon.Height));
            float3 CurrentPixel = inputImage[CurPixelPos].xyz;
            
            // Bilateral with depth
            float2 CurUV = CurPixelPos / float2(ComputeCommon.Width, ComputeCommon.Height);
            float DepthDifference = abs(DepthBuffer.SampleLevel(DepthSampler, CurUV, 0).x - CenterDepth);
            float Gi = Gaussian1D(DepthDifference, ComputeCommon.SigmaForBilateral);
            
            #if USE_GAUSSIAN_INSTEAD
            Color += Gs * CurrentPixel;
            TotalWeight += Gs;
            #else
            Color += Gs * Gi * CurrentPixel;
            TotalWeight += Gs * Gi;
            #endif
        }
    }
    Color /= TotalWeight;
    resultImage[int2(GlobalInvocationID.xy)] = float4(Color, 1.0);
}
