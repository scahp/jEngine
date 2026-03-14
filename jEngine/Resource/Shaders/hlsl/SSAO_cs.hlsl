#include "common.hlsl"

#ifndef SSAO_KERNEL_SIZE
#error "SSAO_KERNEL_SIZE is not defined"
#endif // SSAO_KERNEL_SIZE

float Random_0_1(float co) { return frac(sin(co*(91.3458)) * 47453.5453); }

float3 random_in_unit_sphere(float2 InUV, int InSeed)
{
    float r = Random_0_1(InSeed) + 0.01f;        // To avoid 0 value for random variable
    float2 uv = InUV + float2(r, r * 2.0f);
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * r * 2.0f)) * 43758.5453));
    float noiseZ = (frac(sin(dot(uv, float2(12.9898, 78.233) * r)) * 43758.5453));

    float3 randomUniSphere = float3(noiseX, noiseY, noiseZ) * 2.0f - 1.0f;
    if (length(randomUniSphere) <= 1.0f)
        return randomUniSphere;

    return normalize(randomUniSphere);
}

float3 random_in_hemisphere(float3 InNormal, float2 InUV, int InSeed)
{
    float3 in_unit_sphere = random_in_unit_sphere(InUV, InSeed);
    if (dot(in_unit_sphere, InNormal) > 0.0)  // 노멀 기준으로 같은 반구 방향인지?
        return in_unit_sphere;

    return -in_unit_sphere;
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    float2 uv = GlobalInvocationID.xy / float2(ComputeCommon.Width - 1, ComputeCommon.Height - 1);

    // Get view-space Pos, Normal
    float3 ViewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, ComputeCommon.InvP);
    float3 WorldNormal = normalize(GBuffer0_Normal.SampleLevel(GBuffer0_NormalSampler, uv, 0).xyz * 2.0 - 1.0);
    float3 ViewNormal = mul((float3x3) ComputeCommon.V, WorldNormal);
    
    float2 NoiseUV = ComputeCommon.NoiseUVScale * uv;
    float3 RandomVector = Noise.SampleLevel(NoiseSampler, NoiseUV * uv, 0).xyz * 2.0 - 1.0;
    
    // Orienting random vector to uppper side hemisphere which centered ViewNormal
    float3 tangent = normalize(RandomVector - ViewNormal * dot(RandomVector, ViewNormal));
    float3 bitangent = cross(tangent, ViewNormal);
    float3x3 TBN = float3x3(tangent, bitangent, ViewNormal);
    TBN = transpose(TBN);
    
    // Calculate occlusion value
    float occlusion = 0.0f;	

    for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
    {
        float3 RandomVecInViewNormal = mul(TBN, Kernel.Data[i].xyz);
        float3 samplePos = ViewPos.xyz + RandomVecInViewNormal * ComputeCommon.Radius;
		
		// project to clip space
        float4 offset = float4(samplePos, 1.0f);
        offset = mul(ComputeCommon.P, offset);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5f + 0.5f;
        offset.y = 1.0 - offset.y;
		
        // Get current sample's Depth in view-space
        float3 ViewSamplePos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, offset.xy, ComputeCommon.InvP);
        float SampleDepth = ViewSamplePos.z;
        
        // To remove banding
        float rangeCheck = smoothstep(0.0f, 1.0f, ComputeCommon.Radius / abs(ViewPos.z - SampleDepth));
        occlusion += (SampleDepth < samplePos.z - ComputeCommon.Bias ? 1.0f : 0.0f) * rangeCheck;
    }
    occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
	
    Result[GlobalInvocationID.xy] = float4(occlusion, 0, 0, 1.0);
}
