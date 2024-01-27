Texture2D inputImage : register(t0);
RWTexture2D<float4> resultImage : register(u1);

struct CommonComputeUniformBuffer
{
    float Width;
    float Height;
    float UseWaveIntrinsics;
    int KernalSize;
};

cbuffer ComputeCommon : register(b2)
{
    CommonComputeUniformBuffer ComputeCommon;
}

struct jKernel
{
    float4 Data[20];
};
cbuffer KernelBuffer : register(b3) { jKernel Kernal; }

[numthreads(16, 16, 1)]
void Vertical(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int kernelSize = ComputeCommon.KernalSize;
	int center = ComputeCommon.KernalSize / 2;
    int2 PixelPos = int2(GlobalInvocationID.xy);

    float TotalWeight = 0;
    float4 Color = float4(0, 0, 0, 0);
	for (int j = 0; j < kernelSize; ++j)
    {
        float4 K = Kernal.Data[j / 4];
        float CurrentKernel = K[j % 4];
        int y = (j - center);
        
        float2 CurPixelPos = clamp(PixelPos + float2(0, y), float2(0, 0), float2(ComputeCommon.Width, ComputeCommon.Height));
        Color += inputImage[CurPixelPos] * CurrentKernel;
        
        TotalWeight += CurrentKernel;
    }
    Color /= TotalWeight;
    resultImage[int2(GlobalInvocationID.xy)] = Color;
}

[numthreads(16, 16, 1)]
void Horizon(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int kernelSize = ComputeCommon.KernalSize;
	int center = ComputeCommon.KernalSize / 2;
    int2 PixelPos = int2(GlobalInvocationID.xy);

    float TotalWeight = 0;
    float4 Color = float4(0, 0, 0, 0);
	for (int j = 0; j < kernelSize; ++j)
    {
        float4 K = Kernal.Data[j / 4];
        float CurrentKernel = K[j % 4];
        int x = (j - center);
        
        float2 CurPixelPos = clamp(PixelPos + float2(x, 0), float2(0, 0), float2(ComputeCommon.Width, ComputeCommon.Height));
        Color += inputImage[CurPixelPos] * CurrentKernel;
        
        TotalWeight += CurrentKernel;
    }
    Color /= TotalWeight;
    resultImage[int2(GlobalInvocationID.xy)] = Color;
}

