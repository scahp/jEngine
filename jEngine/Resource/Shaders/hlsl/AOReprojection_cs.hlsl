
#ifndef USE_DISCONTINUITY_WEIGHT
#define USE_DISCONTINUITY_WEIGHT 0
#endif // USE_DISCONTINUITY_WEIGHT

#if COMPUTE_SHADER
float3 GetTexture(RWTexture2D<float4> Tex, int2 Pos)
{
    //return (Tex[Pos].xyz + Tex[Pos + int2(1, 0)].xyz + Tex[Pos + int2(-1, 0)].xyz + Tex[Pos + int2(0, 1)].xyz + Tex[Pos + int2(0, -1)].xyz) / 5.0f;
    return Tex[Pos].xyz;
}

float3 GetTexture(Texture2D Tex, int2 Pos)
{
    //return (Tex[Pos].xyz + Tex[Pos + int2(1, 0)].xyz + Tex[Pos + int2(-1, 0)].xyz + Tex[Pos + int2(0, 1)].xyz + Tex[Pos + int2(0, -1)].xyz) / 5.0f;
    return Tex[Pos].xyz;
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int2 PixelPos = int2(GlobalInvocationID.xy);
    int2 ScreenOffsetToPrevPos = round(VelocityBuffer[PixelPos].xy);
    int2 OldPixelPos = PixelPos - ScreenOffsetToPrevPos;

    if (OldPixelPos.x >= ComputeCommon.Width - 2 || OldPixelPos.y >= ComputeCommon.Height - 2 || OldPixelPos.x < 0 || OldPixelPos.y < 0)
        return;

    float currentColor = resultImage[PixelPos].x;
    float historyColor = HistoryBuffer[OldPixelPos].x;
   
    float ReprojectionWeight = 0.9;
#if USE_DISCONTINUITY_WEIGHT
    float DiscontinuityWeight = abs(DepthBuffer[PixelPos].x - HistoryDepthBuffer[PixelPos].x) < 0.01;
    ReprojectionWeight *= DiscontinuityWeight;
    HistoryDepthBuffer[PixelPos].x = DepthBuffer[PixelPos].x;
#endif
    
    resultImage[PixelPos].x = lerp(currentColor, historyColor, ReprojectionWeight);
}
#endif // COMPUTE_SHADER

#if PIXEL_SHADER
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float AOReprojectionPS(VSOutput input) : SV_TARGET
{
    //float2 ScreenOffsetToPrevUV = (VelocityBuffer.Sample(TextureSampler, input.TexCoord).xy * float2(2.0, 2.0) - float2(1.0, 1.0));
    float2 ScreenOffsetToPrevUV = VelocityBuffer.Sample(VelocityBufferSampler, input.TexCoord).xy;
    float2 OldUV = input.TexCoord - ScreenOffsetToPrevUV;
    
    float currentColor = CurrentTexture.Sample(CurrentTextureSampler, input.TexCoord).x;
    float historyColor = HistoryBuffer.Sample(HistoryBufferSampler, OldUV).x;
    
    float ReprojectionWeight = 0.9;
#if USE_DISCONTINUITY_WEIGHT
    float DiscontinuityWeight = abs(DepthBuffer.Sample(DepthBufferSampler, input.TexCoord).x - HistoryDepthBuffer.Sample(HistoryDepthBufferSampler, input.TexCoord).x) < 0.01;
    ReprojectionWeight *= DiscontinuityWeight;
#endif // USE_DISCONTINUITY_WEIGHT
    
    return lerp(currentColor, historyColor, ReprojectionWeight);
}
#endif // PIXEL_SHADER
