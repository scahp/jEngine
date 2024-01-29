
#ifndef SHOW_AO_ONLY
#define SHOW_AO_ONLY 0
#endif // SHOW_AO_ONLY

struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    float AOIntensity;
    int Padding0;
};

#if COMPUTE_SHADER
RWTexture2D<float4> resultImage : register(u0);
Texture2D inputImage : register(t1);

cbuffer ComputeCommon : register(b2) { CommonComputeUniformBuffer ComputeCommon; }

[numthreads(16, 16, 1)]
void AOApplyCS(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

#if SHOW_AO_ONLY
    resultImage[int2(GlobalInvocationID.xy)].xyz = lerp(float3(1.0f, 1.0f, 1.0f), inputImage[uint2(GlobalInvocationID.xy)].xyz, ComputeCommon.AOIntensity);
#else
    resultImage[int2(GlobalInvocationID.xy)].xyz *= lerp(float3(1.0f, 1.0f, 1.0f), inputImage[uint2(GlobalInvocationID.xy)].xyz, ComputeCommon.AOIntensity);
#endif
}
#endif // COMPUTE_SHADER

#if PIXEL_SHADER
Texture2D AOTexture : register(t0);
SamplerState AOSampler : register(s0);

cbuffer ComputeCommon : register(b1) { CommonComputeUniformBuffer ComputeCommon; }

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 AOApplyPS(VSOutput input) : SV_TARGET
{
    float AO = AOTexture.Sample(AOSampler, input.TexCoord).x;
    //AO = lerp(1.0, AO, ComputeCommon.AOIntensity);
    return float4(float3(1, 1, 1), AO);
}
#endif // PIXEL_SHADER

