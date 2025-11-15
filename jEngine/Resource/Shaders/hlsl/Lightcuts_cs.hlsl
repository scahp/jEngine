#include "common.hlsl"

//////////////////////////////////////////////////////////////////////////
// Lightcuts Compute Shader
//
// GPU-based lightcut evaluation for many-light rendering.
//
// Phase 5 Implementation:
// - Reads GBuffer (position, normal, material)
// - Evaluates a pre-computed lightcut (from CPU)
// - Writes illumination to output buffer
//
// Future optimization:
// - Per-pixel lightcut selection on GPU
// - Light tree traversal in compute shader
//////////////////////////////////////////////////////////////////////////

// Constants matching jLightcutTypes.h
#define LIGHTCUT_MAX_CUT_SIZE 1000
#define EPSILON 1e-6
#define PI 3.141592653

// Light types
#define LIGHTTYPE_OMNI 0
#define LIGHTTYPE_ORIENTED 1
#define LIGHTTYPE_DIRECTIONAL 2

//////////////////////////////////////////////////////////////////////////
// Structures
//////////////////////////////////////////////////////////////////////////

struct LightcutsUniformBuffer
{
    float4x4 InvViewProj;
    float4x4 View;
    float4x4 Proj;

    int Width;
    int Height;
    int CutSize;              // Number of nodes in the lightcut
    int TraceShadows;         // Whether to trace shadow rays

    float3 CameraPos;
    float ErrorRatio;         // Error threshold (e.g., 0.02)
};

// Simplified light cluster node (GPU-friendly)
struct LightCluster
{
    float3 Position;          // Representative light position
    float Intensity;          // Cluster total intensity

    float3 Color;             // Light color
    uint LightType;           // 0=Omni, 1=Oriented, 2=Directional

    float3 Direction;         // For directional/spot lights
    float Padding0;
};

//////////////////////////////////////////////////////////////////////////
// Resources
//////////////////////////////////////////////////////////////////////////

// Output
RWTexture2D<float4> OutputColor : register(u0, space0);

// GBuffer inputs
Texture2D<float4> GBuffer0_WorldPos : register(t1, space0);
SamplerState GBuffer0_WorldPosSampler : register(s1, space0);

Texture2D<float4> GBuffer1_Normal : register(t2, space0);
SamplerState GBuffer1_NormalSampler : register(s2, space0);

Texture2D<float4> GBuffer2_Albedo : register(t3, space0);
SamplerState GBuffer2_AlbedoSampler : register(s3, space0);

Texture2D<float> DepthTexture : register(t4, space0);
SamplerState DepthSampler : register(s4, space0);

// Lightcut data (structured buffer)
StructuredBuffer<LightCluster> LightcutNodes : register(t5, space0);

// Uniform buffer
cbuffer LightcutsCommon : register(b6, space0)
{
    LightcutsUniformBuffer Uniforms;
}

//////////////////////////////////////////////////////////////////////////
// Lighting Evaluation
//////////////////////////////////////////////////////////////////////////

// Evaluate material term (simplified diffuse BRDF)
float EvaluateMaterialTerm(float3 normal, float3 lightDir)
{
    float NdotL = max(0.0, dot(normal, lightDir));
    return NdotL / PI;  // Lambertian BRDF
}

// Evaluate geometric term (distance attenuation)
float EvaluateGeometricTerm(float3 lightPos, float3 shadingPos, uint lightType)
{
    if (lightType == LIGHTTYPE_DIRECTIONAL)
    {
        return 1.0;  // No attenuation for directional lights
    }

    float3 diff = lightPos - shadingPos;
    float distSq = max(EPSILON, dot(diff, diff));
    return 1.0 / distSq;  // Inverse square falloff
}

// Evaluate visibility (placeholder for Phase 5)
float EvaluateVisibility(float3 lightPos, float3 shadingPos, uint lightType)
{
    // Phase 5: Always return 1.0 (no shadows)
    // Phase 6: Implement ray tracing
    return 1.0;
}

// Get light direction from shading point to light
float3 GetLightDirection(LightCluster cluster, float3 shadingPos)
{
    if (cluster.LightType == LIGHTTYPE_DIRECTIONAL)
    {
        return -cluster.Direction;  // Direction TO light
    }
    else
    {
        float3 diff = cluster.Position - shadingPos;
        return normalize(diff);
    }
}

// Evaluate a single light cluster
float3 EvaluateCluster(LightCluster cluster, float3 shadingPos, float3 normal, float3 viewDir)
{
    // Get light direction
    float3 lightDir = GetLightDirection(cluster, shadingPos);

    // Evaluate material term (BRDF × cos)
    float M = EvaluateMaterialTerm(normal, lightDir);
    if (M < EPSILON)
        return float3(0, 0, 0);

    // Evaluate geometric term (distance attenuation)
    float G = EvaluateGeometricTerm(cluster.Position, shadingPos, cluster.LightType);
    if (G < EPSILON)
        return float3(0, 0, 0);

    // Evaluate visibility (shadow test)
    float V = EvaluateVisibility(cluster.Position, shadingPos, cluster.LightType);
    if (V < EPSILON)
        return float3(0, 0, 0);

    // Combine: L = M × G × V × I × color
    float3 contribution = cluster.Color * (M * G * V * cluster.Intensity);

    return contribution;
}

//////////////////////////////////////////////////////////////////////////
// Main Compute Shader
//////////////////////////////////////////////////////////////////////////

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    // Bounds check
    if (GlobalInvocationID.x >= Uniforms.Width || GlobalInvocationID.y >= Uniforms.Height)
        return;

    uint2 pixelCoord = GlobalInvocationID.xy;
    float2 uv = (float2(pixelCoord) + 0.5) / float2(Uniforms.Width, Uniforms.Height);

    // Read GBuffer
    float depth = DepthTexture.SampleLevel(DepthSampler, uv, 0);

    // Early out for sky/background
    if (depth >= 1.0)
    {
        OutputColor[pixelCoord] = float4(0, 0, 0, 1);
        return;
    }

    float3 worldPos = GBuffer0_WorldPos.SampleLevel(GBuffer0_WorldPosSampler, uv, 0).xyz;
    float3 normal = GBuffer1_Normal.SampleLevel(GBuffer1_NormalSampler, uv, 0).xyz;
    float3 albedo = GBuffer2_Albedo.SampleLevel(GBuffer2_AlbedoSampler, uv, 0).rgb;

    // Normalize normal (may be compressed in GBuffer)
    normal = normalize(normal);

    // View direction
    float3 viewDir = normalize(Uniforms.CameraPos - worldPos);

    // Evaluate lightcut
    float3 totalIllumination = float3(0, 0, 0);

    for (int i = 0; i < Uniforms.CutSize; ++i)
    {
        LightCluster cluster = LightcutNodes[i];
        float3 contribution = EvaluateCluster(cluster, worldPos, normal, viewDir);
        totalIllumination += contribution;
    }

    // Apply albedo (diffuse color)
    float3 finalColor = totalIllumination * albedo;

    // Clamp to positive values
    finalColor = max(float3(0, 0, 0), finalColor);

    // Write output
    OutputColor[pixelCoord] = float4(finalColor, 1.0);
}
