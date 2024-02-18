
#ifndef USE_DISCONTINUITY_WEIGHT
#define USE_DISCONTINUITY_WEIGHT 0
#endif // USE_DISCONTINUITY_WEIGHT

struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    int FrameNumber;
    float InvScaleToOriginBuffer;
};

#if COMPUTE_SHADER
RWTexture2D<float4> resultImage : register(u0);
Texture2D HistoryBuffer : register(t1);
Texture2D VelocityBuffer : register(t2);
Texture2D DepthBuffer : register(t3);
RWTexture2D<float> HistoryDepthBuffer : register(u4);

cbuffer ComputeCommon : register(b5)
{
    CommonComputeUniformBuffer ComputeCommon;
}

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

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    int2 PixelPos = int2(GlobalInvocationID.xy);
    int2 ScreenOffsetToPrevPos = round(VelocityBuffer[PixelPos].xy);
    int2 OldPixelPos = PixelPos - ScreenOffsetToPrevPos;

    if (OldPixelPos.x >= ComputeCommon.Width - 2 || OldPixelPos.y >= ComputeCommon.Height - 2 || OldPixelPos.x < 0 || OldPixelPos.y < 0)
        return;

    float3 currentColor = resultImage[PixelPos].xyz;
    float3 historyColor = HistoryBuffer[OldPixelPos].xyz;
   
    //float3 currentColor = GetTexture(resultImage, PixelPos);
    //float3 historyColor = GetTexture(HistoryBuffer, PixelPos);
    
    float ReprojectionWeight = 0.9;
#if USE_DISCONTINUITY_WEIGHT
    float DiscontinuityWeight = abs(DepthBuffer[PixelPos].x - HistoryDepthBuffer[PixelPos].x) < 0.01;
    ReprojectionWeight *= DiscontinuityWeight;
    HistoryDepthBuffer[PixelPos].x = DepthBuffer[PixelPos].x;
#endif
    
    resultImage[PixelPos].xyz = lerp(currentColor, historyColor, ReprojectionWeight);
}
#endif // COMPUTE_SHADER

#if PIXEL_SHADER
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

Texture2D CurrentTexture : register(t0);
SamplerState TextureSampler : register(s0);
Texture2D HistoryBuffer : register(t1);
Texture2D VelocityBuffer : register(t2);
Texture2D DepthBuffer : register(t3);
Texture2D HistoryDepthBuffer : register(t4);

cbuffer ComputeCommon : register(b5)
{
    CommonComputeUniformBuffer ComputeCommon;
}

float4 AOReprojectionPS(VSOutput input) : SV_TARGET
{
    float2 Velocity = VelocityBuffer.Sample(TextureSampler, input.TexCoord).xy;
    float2 OldUV = input.TexCoord - Velocity;
    
    float3 currentColor = CurrentTexture.Sample(TextureSampler, input.TexCoord).xyz;
    float3 historyColor = HistoryBuffer.Sample(TextureSampler, OldUV).xyz;
    
    float ReprojectionWeight = 0.9;
#if USE_DISCONTINUITY_WEIGHT
    float DiscontinuityWeight = abs(DepthBuffer.Sample(TextureSampler, input.TexCoord).x - HistoryDepthBuffer.Sample(TextureSampler, input.TexCoord).x) < 0.01;
    ReprojectionWeight *= DiscontinuityWeight;
#endif // USE_DISCONTINUITY_WEIGHT
    
    return float4(lerp(currentColor, historyColor, ReprojectionWeight), 1.0);
}
#endif // PIXEL_SHADER