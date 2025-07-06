#include "common.hlsl"

#ifndef SSGI_MAX_STEPS
#define SSGI_MAX_STEPS 64
#endif

#ifndef SSGI_MAX_DISTANCE
#define SSGI_MAX_DISTANCE 100.0
#endif

#ifndef SSGI_THICKNESS
#define SSGI_THICKNESS 0.1
#endif

struct CommonComputeUniformBuffer
{
    float4x4 InvP;
    float4x4 V;
    float4x4 P;
    float Radius;
    float Bias;
    float2 NoiseUVScale;
    int Width;
    int Height;
    int FrameNumber;
    int Padding0;
    float3 CameraPos;
    float Padding1;
};

RWTexture2D<float4> Result : register(u0, space0);

Texture2D DepthTexture : register(t1, space0);
SamplerState DepthTextureSamplerState : register(s1, space0);

// GBuffer0: Normal(xyz), Metallic(w)
Texture2D GBuffer0 : register(t2, space0);
SamplerState GBuffer0SamplerState : register(s2, space0);

// ColorPtr
Texture2D ColorTexture : register(t3, space0);
SamplerState ColorTextureSamplerState : register(s3, space0);

Texture2D Noise : register(t4, space0);
SamplerState NoiseSamplerState : register(s4, space0);

cbuffer ComputeCommon : register(b5, space0)
{
    CommonComputeUniformBuffer ComputeCommon;
}

// A simple pseudo-random number generator using frame number as a seed
float2 rand(float2 co)
{
    return float2(
        frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453),
        frac(sin(dot(co, float2(4.898, 7.23))) * 23421.631)
    );
}

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    float2 uv = GlobalInvocationID.xy / float2(ComputeCommon.Width - 1, ComputeCommon.Height - 1);

    // Get view-space position and normal
    float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSamplerState, uv, ComputeCommon.InvP);
    float3 worldNormal = normalize(GBuffer0.SampleLevel(GBuffer0SamplerState, uv, 0).xyz * 2.0 - 1.0);
    float3 viewNormal = normalize(mul((float3x3)ComputeCommon.V, worldNormal));

    // Use a combination of screen position and frame number for a time-varying seed
    float2 randomSeed = GlobalInvocationID.xy + float2(GlobalInvocationID.x * ComputeCommon.FrameNumber % 256, GlobalInvocationID.y * ComputeCommon.FrameNumber % 14);
    float2 randomValues = rand(randomSeed);

    // Create a TBN matrix oriented to the normal vector
    float3 tangent = (abs(viewNormal.y) < 0.999) ? normalize(cross(viewNormal, float3(0, 1, 0))) : float3(1, 0, 0);
    float3 bitangent = cross(viewNormal, tangent);
    float3x3 tbn = float3x3(tangent, bitangent, viewNormal);

    // Generate a cosine-weighted random vector in the hemisphere
    float r1 = 2.0 * PI * randomValues.x;
    float r2 = randomValues.y;
    float r2s = sqrt(r2);
    float3 sampleDir = mul(tbn, float3(cos(r1) * r2s, sin(r1) * r2s, sqrt(1.0 - r2)));

    float3 indirectLight = float3(0.0, 0.0, 0.0);
    
    // Screen-space ray marching
    // Use noise texture for jittering the starting position of the ray to reduce banding
    float2 noiseUV = ComputeCommon.NoiseUVScale * uv;
    float rayJitter = Noise.SampleLevel(NoiseSamplerState, noiseUV, 0).r;

    viewPos += sampleDir * 50;

    float stepSize = SSGI_MAX_DISTANCE / SSGI_MAX_STEPS;
    for (int i = 0; i < SSGI_MAX_STEPS; ++i)
    {
        // March along the reflection vector in view space
        float3 rayPos = viewPos + sampleDir * stepSize * (float(i) + rayJitter);

        // Project ray position to screen space
        float4 projectedPos = mul(ComputeCommon.P, float4(rayPos, 1.0));
        projectedPos.xyz /= projectedPos.w;
        float2 rayUV = projectedPos.xy * 0.5 + 0.5;
        rayUV.y = 1.0 - rayUV.y;

        // Check if the ray is within the screen bounds
        if (rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0)
        {
            break;
        }

        // Get depth at the ray's screen position
        float sceneDepth = CalcViewPositionFromDepth(DepthTexture, DepthTextureSamplerState, rayUV, ComputeCommon.InvP).z;

        // Check for intersection
        if (sceneDepth < rayPos.z && (rayPos.z - sceneDepth) < SSGI_THICKNESS)
        {
            // Intersection found, get color from ColorPtr
            float3 hitColor = ColorTexture.SampleLevel(ColorTextureSamplerState, rayUV, 0).rgb;
            
            // Get normal at hit point
            float3 hitWorldNormal = normalize(GBuffer0.SampleLevel(GBuffer0SamplerState, rayUV, 0).xyz * 2.0 - 1.0);
            float3 hitViewNormal = normalize(mul((float3x3)ComputeCommon.V, hitWorldNormal));

            // Cosine falloff
            float cosFalloff = saturate(dot(normalize(viewPos - rayPos), hitViewNormal));

            // Attenuate light by distance
            float distFalloff = 1.0 - smoothstep(0.0, SSGI_MAX_DISTANCE, length(rayPos - viewPos));
            
            // indirectLight = hitColor * cosFalloff * distFalloff;
            indirectLight = hitColor * 10.0f;
            break;
        }
    }

    Result[GlobalInvocationID.xy] = float4(indirectLight, 1.0);
}


