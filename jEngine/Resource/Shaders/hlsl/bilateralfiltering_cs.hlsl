
#ifndef USE_GAUSSIAN_INSTEAD
#define USE_GAUSSIAN_INSTEAD 0
#endif // USE_GAUSSIAN_INSTEAD

Texture2D inputImage : register(t0);
RWTexture2D<float4> resultImage : register(u1);

struct CommonComputeUniformBuffer
{
    float Width;
    float Height;
    float Sigma;
    int KernalSize;
    float BilateralIntensityScale;
    float3 Padding0;
};

cbuffer ComputeCommon : register(b2)
{
    CommonComputeUniformBuffer ComputeCommon;
}

float Gaussian(float ValueSq, float sigma)
{
    return exp(-0.5f * ValueSq / (sigma * sigma)) / (2 * 3.14 * sigma * sigma);
}

float GetIntensity(float3 LinearColor)
{
    return (LinearColor.x + LinearColor.y + LinearColor.z) / 3.0f;
}

[numthreads(16, 16, 1)]
void Bilateral(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int kernelSize = ComputeCommon.KernalSize;
    int center = kernelSize / 2;
    int2 PixelPos = int2(GlobalInvocationID.xy);
    
    float CenterPixelLuminance = GetIntensity(inputImage[PixelPos].xyz);
    
    float3 Color = float3(0, 0, 0);
    float TotalWeight = 0;
    for (int i = 0; i < kernelSize; ++i)
    {
        for (int j = 0; j < kernelSize; ++j)
        {
            int x = (i - center);
            int y = (j - center);
            float Gs = Gaussian(x * x + y * y, ComputeCommon.Sigma);
            
            float2 CurPixelPos = clamp(PixelPos + float2(x, y), float2(0, 0), float2(ComputeCommon.Width, ComputeCommon.Height));
            float3 CurrentPixel = inputImage[CurPixelPos].xyz;
            
            float PixelIntensityDifference = (GetIntensity(CurrentPixel) - CenterPixelLuminance) * ComputeCommon.BilateralIntensityScale;
            float Gi = Gaussian(PixelIntensityDifference * PixelIntensityDifference, ComputeCommon.Sigma);
            
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
