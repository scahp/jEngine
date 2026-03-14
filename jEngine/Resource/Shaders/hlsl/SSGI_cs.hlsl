#include "common.hlsl"
#include "PBR.hlsl"

#ifndef SSGI_MAX_STEPS
#define SSGI_MAX_STEPS 32
#endif

#ifndef SSGI_MAX_DISTANCE
#define SSGI_MAX_DISTANCE 100.0
#endif

#ifndef SSGI_THICKNESS
#define SSGI_THICKNESS 0.1
#endif

// A simple pseudo-random number generator using frame number as a seed
float2 rand(float2 co)
{
    return float2(
        frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453),
        frac(sin(dot(co, float2(4.898, 7.23))) * 23421.631)
    );
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;

    float2 uv = GlobalInvocationID.xy / float2(ComputeCommon.Width - 1, ComputeCommon.Height - 1);

    // Get view-space position and normal
    float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, ComputeCommon.InvP);
    float3 worldPos = mul(ComputeCommon.InvV, float4(viewPos, 1.0)).xyz;
    float3 worldNormal = normalize(GBuffer0.SampleLevel(GBuffer0Sampler, uv, 0).xyz * 2.0 - 1.0);
    float3 viewNormal = normalize(mul((float3x3)ComputeCommon.V, worldNormal));

    // Get material properties at current position
    float3 albedo = GBuffer1.SampleLevel(GBuffer1Sampler, uv, 0).xyz;
    float4 gbuffer2 = GBuffer2.SampleLevel(GBuffer2Sampler, uv, 0);
    float metallic = gbuffer2.z;
    float roughness = gbuffer2.w;

    // View direction for PBR
    float3 V = normalize(ComputeCommon.CameraPos - worldPos);

    // Create a TBN matrix oriented to the normal vector
    float3 tangent = (abs(viewNormal.y) < 0.999) ? normalize(cross(viewNormal, float3(0, 1, 0))) : float3(1, 0, 0);
    float3 bitangent = cross(viewNormal, tangent);
    float3x3 tbn = float3x3(tangent, bitangent, viewNormal);
    tbn = transpose(tbn);

    // Screen-space ray marching
    // Use noise texture for jittering the starting position of the ray to reduce banding
    float2 noiseUV = ComputeCommon.NoiseUVScale * uv;
    float rayJitter = Noise.SampleLevel(NoiseSampler, noiseUV, 0).r;
    
    uint seed = InitRandomSeed(GlobalInvocationID.xy, float2(ComputeCommon.Width, ComputeCommon.Height), ComputeCommon.FrameNumber);
    
    float3 indirectLight = float3(0.0, 0.0, 0.0);
    float count = 0.0f;
    for (int k = 0; k < ComputeCommon.SSGI_RayCount; ++k)
    {
        // Use a combination of screen position and frame number for a time-varying seed
        float2 randomValues = float2(Random_0_1(seed), Random_0_1(seed));

        // Generate a cosine-weighted random vector in the hemisphere
        float r1 = 2.0 * PI * randomValues.x;
        float r2 = randomValues.y;
        float r2s = sqrt(r2);
        float3 sampleDir = mul(tbn, float3(cos(r1) * r2s, sin(r1) * r2s, sqrt(1.0 - r2)));

        viewPos += sampleDir * 50;

        float stepSize = ComputeCommon.SSGI_MaxDistance / ComputeCommon.SSGI_MaxSteps;
        for (int i = 0; i < ComputeCommon.SSGI_MaxSteps; ++i)
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
            float sceneDepth = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, rayUV, ComputeCommon.InvP).z;

            // Check for intersection
            if (sceneDepth < rayPos.z && (rayPos.z - sceneDepth) < SSGI_THICKNESS)
            {
                // Intersection found, get incoming radiance from hit surface
                float3 hitColor = ColorTexture.SampleLevel(ColorTextureSampler, rayUV, 0).rgb;

                // Get hit position in world space
                float3 hitViewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, rayUV, ComputeCommon.InvP);
                float3 hitWorldPos = mul(ComputeCommon.InvV, float4(hitViewPos, 1.0)).xyz;

                // Get normal at hit point for visibility test
                float3 hitWorldNormal = normalize(GBuffer0.SampleLevel(GBuffer0Sampler, rayUV, 0).xyz * 2.0 - 1.0);
                //float3 hitViewNormal = normalize(mul((float3x3) ComputeCommon.V, hitWorldNormal));

                // Visibility: check if hit surface faces toward receiver 
                float3 hitToReceiver = normalize(worldPos - hitWorldPos);
                float visibility = saturate(dot(hitWorldNormal, hitToReceiver));

                // Distance attenuation
                float distToHit = length(hitWorldPos - worldPos);
                float distFalloff = 1.0 - smoothstep(0.0, ComputeCommon.SSGI_MaxDistance, distToHit * 0.09);

                float attenuation = ComputeCommon.UseAttenuation ? (visibility * distFalloff) : 1.0;
                indirectLight += hitColor * attenuation;
                count += 1.0f;
                break;
            }
        }
    }
    
    if (count > 0)
    {
        indirectLight /= count;
    }

    Result[GlobalInvocationID.xy] = float4(indirectLight, 1.0);
}


