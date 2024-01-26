RWTexture2D<float4> resultImage : register(u0);
Texture2D inputImage : register(t1);
Texture2D VelocityBuffer : register(t2);

struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    int UseWaveIntrinsics;
    float Padding0;
};

cbuffer ComputeCommon : register(b3) { CommonComputeUniformBuffer ComputeCommon; }

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int2 PixelPos = int2(GlobalInvocationID.xy);
    int2 ScreenOffsetToPrevPos = VelocityBuffer[PixelPos].xy;
    int2 OldPixelPos = PixelPos - ScreenOffsetToPrevPos;

    if (OldPixelPos.x >= ComputeCommon.Width-2 || OldPixelPos.y >= ComputeCommon.Height-2 || OldPixelPos.x < 0 || OldPixelPos.y < 0)
        return;

    float3 currentColor = resultImage[PixelPos].xyz;
    float3 historyColor = inputImage[OldPixelPos];

    resultImage[PixelPos].xyz = lerp(resultImage[PixelPos].xyz, historyColor, 0.9);
}
