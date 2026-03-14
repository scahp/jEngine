
#ifndef SHOW_AO_ONLY
#define SHOW_AO_ONLY 0
#endif // SHOW_AO_ONLY

#if COMPUTE_SHADER
[numthreads(8, 8, 1)]
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
struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 AOApplyPS(VSOutput input) : SV_TARGET
{
    float AO = AOTexture.Sample(AOTextureSampler, input.TexCoord).x;
    AO = lerp(1.0, AO, ComputeCommon.AOIntensity);
    return float4(float3(1, 1, 1), AO);
}
#endif // PIXEL_SHADER

