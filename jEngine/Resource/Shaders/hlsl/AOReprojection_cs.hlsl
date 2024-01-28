
#ifndef USE_DISCONTINUITY_WEIGHT
#define USE_DISCONTINUITY_WEIGHT 0
#endif // USE_DISCONTINUITY_WEIGHT

RWTexture2D<float4> resultImage : register(u0);
Texture2D HistoryBuffer : register(t1);
Texture2D VelocityBuffer : register(t2);
Texture2D DepthBuffer : register(t3);
RWTexture2D<float> HistoryDepthBuffer : register(u4);

struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    int UseWaveIntrinsics;
    float Padding0;
};

cbuffer ComputeCommon : register(b5) { CommonComputeUniformBuffer ComputeCommon; }

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int2 PixelPos = int2(GlobalInvocationID.xy);
    int2 ScreenOffsetToPrevPos = round(VelocityBuffer[PixelPos].xy);
    int2 OldPixelPos = PixelPos - ScreenOffsetToPrevPos;

    if (OldPixelPos.x >= ComputeCommon.Width-2 || OldPixelPos.y >= ComputeCommon.Height-2 || OldPixelPos.x < 0 || OldPixelPos.y < 0)
        return;

    float3 currentColor = resultImage[PixelPos].xyz;
    float3 historyColor = HistoryBuffer[OldPixelPos].xyz;
    
    float ReprojectionWeight = 0.9;
    #if USE_DISCONTINUITY_WEIGHT
    float DiscontinuityWeight = abs(DepthBuffer[PixelPos].x - HistoryDepthBuffer[PixelPos].x) < 0.01;
    ReprojectionWeight *= DiscontinuityWeight;
    HistoryDepthBuffer[PixelPos].x = DepthBuffer[PixelPos].x;
    #endif
    
    resultImage[PixelPos].xyz = lerp(resultImage[PixelPos].xyz, historyColor, ReprojectionWeight);
}
