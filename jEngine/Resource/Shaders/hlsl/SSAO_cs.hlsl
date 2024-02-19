#include "common.hlsl"

struct CommonComputeUniformBuffer
{
    float4x4 VP;
    float4x4 InvVP;
    float4x4 InvP;
    float4x4 V;
    float4x4 P;
    float Radius;
    float Bias;
    float2 HaltonJitter;
    int Width;
    int Height;
    int FrameNumber;
    int RayPerPixel;
    float4 CameraPos;
};

struct KernelParam
{
    float4 Data[64];
};

RWTexture2D<float4> Result : register(u0, space0);

Texture2D DepthTexture : register(t1, space0);
SamplerState DepthTextureSamplerState : register(s1, space0);

Texture2D GBuffer0_Normal : register(t2, space0);
SamplerState GBuffer0_NormalSamplerState : register(s2, space0);

Texture2D Noise : register(t3, space0);
SamplerState NoiseSamplerState : register(s3, space0);

cbuffer ComputeCommon : register(b4, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
}

cbuffer Kernel : register(b5, space0)
{
    KernelParam Kernel;
}

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

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    float2 uv = GlobalInvocationID.xy / float2(ComputeCommon.Width - 1, ComputeCommon.Height - 1);

    float3 WorldPos = CalcWorldPositionFromDepth(DepthTexture, DepthTextureSamplerState, uv, ComputeCommon.InvVP);
    float2 OctNormal = GBuffer0_Normal.SampleLevel(GBuffer0_NormalSamplerState, uv, 0).xy;
    float3 WorldNormal = normalize(GBuffer0_Normal.SampleLevel(GBuffer0_NormalSamplerState, uv, 0).xyz * 2.0 - 1.0);
    
    float4 ViewPos = mul(ComputeCommon.V, float4(WorldPos, 1.0f));
    float3 ViewNormal = mul((float3x3) ComputeCommon.V, WorldNormal);
    
    int2 texDim;
    DepthTexture.GetDimensions(texDim.x, texDim.y);
    int2 noiseDim;
    Noise.GetDimensions(noiseDim.x, noiseDim.y);
    float2 NoiseUV = float2(float(texDim.x) / float(noiseDim.x), float(texDim.y) / float(noiseDim.y)) * uv;
    float3 randomVec = Noise.SampleLevel(NoiseSamplerState, NoiseUV * uv, 0).xyz * 2.0 - 1.0;
    
    float3 tangent = normalize(randomVec - ViewNormal * dot(randomVec, ViewNormal));
    float3 bitangent = cross(tangent, ViewNormal);
    float3x3 TBN = float3x3(tangent, bitangent, ViewNormal);
    TBN = transpose(TBN);
    
    // Calculate occlusion value
    float occlusion = 0.0f;
	
    float length = 50.0f;
    
    // remove banding
    const float bias = 0.025f * length;
    
#define SSAO_KERNEL_SIZE 64
#define SSAO_RADIUS 0.5 * length
    for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
    {
        float3 vec = mul(TBN, Kernel.Data[i].xyz);
        float3 samplePos = ViewPos.xyz + vec * SSAO_RADIUS;
		
		// project
        float4 offset = float4(samplePos, 1.0f);
        offset = mul(ComputeCommon.P, offset);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5f + 0.5f;
        offset.y = 1.0 - offset.y;
		
        //float sampleDepth = -texture(samplerPositionDepth, offset.xy).w;
        float3 WorldSamplePos = CalcWorldPositionFromDepth(DepthTexture, DepthTextureSamplerState, offset.xy, ComputeCommon.InvVP);
        float4 ViewSamplePos = mul(ComputeCommon.V, float4(WorldSamplePos, 1.0f));
        //ViewSamplePos /= ViewSamplePos.w;
        float SampleDepth = ViewSamplePos.z;
        
        float rangeCheck = smoothstep(0.0f, 1.0f, SSAO_RADIUS / abs(ViewPos.z - SampleDepth));
        occlusion += (SampleDepth < samplePos.z - bias ? 1.0f : 0.0f) * rangeCheck;
        
        //Result[GlobalInvocationID.xy] = float4(ViewNormal.xyz, 1);
        //Result[GlobalInvocationID.xy] = float4(abs(ViewNormal.xyz) * 10, 1);
        //return;
    }
    occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));
	
    Result[GlobalInvocationID.xy] = float4(occlusion, occlusion, occlusion, 1.0);
}

/*
[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    float2 uv = GlobalInvocationID.xy / float2(ComputeCommon.Width - 1, ComputeCommon.Height - 1);

    float3 WorldPos = CalcWorldPositionFromDepth(DepthTexture, DepthTextureSamplerState, uv, ComputeCommon.InvVP);
    float2 OctNormal = GBuffer0_Normal.SampleLevel(GBuffer0_NormalSamplerState, uv, 0).xy;
    float3 WorldNormal = normalize(DecodeOctNormal(OctNormal));
    
    float radius = 50.5f;
    float bias = 7.0f;
    float Occlusion = 0.0;

    for (int i = 0; i < ComputeCommon.RayPerPixel; ++i)
    {
        float3 RandomDir = normalize(random_in_hemisphere(WorldNormal, uv * 2 - 1, i));

        // get sample position
        float3 samplePos = WorldPos + RandomDir * radius * clamp(i / ComputeCommon.RayPerPixel, 0.1, 1.0);//max(5.0f, radius * Random_0_1(ComputeCommon.FrameNumber + 1000 + i));

        // project sample position (to sample texture) (to get position on screen/texture)
        float4 offset = float4(samplePos, 1.0);
        offset = mul(ComputeCommon.VP, offset); // from view to clip-space
        offset.xyz /= offset.w; // perspective divide
        offset.xy = offset.xy * 0.5 + 0.5; // transform to range 0.0 - 1.0
        offset.y = 1.0 - offset.y;

        //offset.xy = clamp(offset.xy, float2(0, 0), float2(1, 1));

        if (offset.x > 1.0 || offset.y > 1.0 || (offset.x < 0.0 || offset.y < 0.0))
        {
            //offset.xy = clamp(offset.xy, float2(0, 0), float2(1, 1));
        }

        // get depth value of kernel sample
        float SampleDepth = DepthTexture.SampleLevel(DepthTextureSamplerState, offset.xy, 0).x;
        float3 SampleWorldDepth = CalcWorldPositionFromDepth(SampleDepth, offset.xy, ComputeCommon.InvVP);
        SampleDepth = length(SampleWorldDepth.xyz - ComputeCommon.CameraPos.xyz);

        float temp = length(WorldPos.xyz - ComputeCommon.CameraPos.xyz);
        // range check & accumulate
        float RangeCheck = 1.0f;
        RangeCheck = smoothstep(0.0, 1.0, radius / abs(temp - SampleDepth));
        Occlusion += (temp >= SampleDepth + bias ? 1.0 : 0.0) * RangeCheck;
    }
    Occlusion = 1.0 - (Occlusion / ComputeCommon.RayPerPixel);
    
    Result[GlobalInvocationID.xy] = float4(Occlusion, 0, 0, 1.0);
}
*/