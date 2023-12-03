#include "common.hlsl"
#include "Sphericalmap.hlsl"
#include "PBR.hlsl"

struct jAtmosphericData
{
    float4x4 ShadowVP;
    float4x4 VP;
    float3 CameraPos;
    float CameraNear;
    float3 LightDirection;
    float CameraFar;
    float AnisoG;
    float SlopeOfDist;
    float InScatteringLambda;
    float Dummy;
    int TravelCount;
    int RTWidth;
    int RTHeight;
    int UseNoise;  // todo : change to #define USE_NOISE
};

Texture2D GBuffer0 : register(t0, space0);
SamplerState GBuffer0SamplerState : register(s0, space0);

Texture2D DepthTexture : register(t1, space0);
SamplerState DepthTextureSamplerState : register(s1, space0);

Texture2D ShadowMapTexture : register(t2, space0);
SamplerState ShadowMapSamplerState : register(s2, space0);

cbuffer AtmosphericParam : register(b3, space0) { jAtmosphericData AtmosphericParam; }

RWTexture2D<float4> Result : register(u4, space0);

float3 TransformShdowMapTextureSpace(float3 InWorldPos)
{
    float4 temp = mul(AtmosphericParam.ShadowVP, float4(InWorldPos, 1.0));
    temp /= temp.w;
    temp.xyz = temp.xyz * 0.5 + float3(0.5, 0.5, 0.5);
    temp.y = 1.0 - temp.y;
    return saturate(temp.xyz);
}

float3 TransformNDC(float3 InWorldPos)
{
    float4 temp = mul(AtmosphericParam.VP, float4(InWorldPos, 1.0));
    temp /= temp.w;
    return saturate(temp.xyz);
}

float rand(float2 co)
{
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

float AnisotropyIntensity(float3 InFromSurfaceToCamera)
{
    float atmosphereBrightness = AtmosphericParam.InScatteringLambda * (AtmosphericParam.CameraFar - AtmosphericParam.CameraNear) / AtmosphericParam.TravelCount; // lambda * (dmax - dmin) / (n + 1)

    float g = AtmosphericParam.AnisoG;
    float3 anisotropyConst = float3(1.0 - g, 1.0 + g * g, 2.0 * g); // (1 - g, 1 + g * g, 2 * g)

    float invLength = 1.0;
    float h = anisotropyConst.x * (1.0 / sqrt(anisotropyConst.y - anisotropyConst.z * dot(InFromSurfaceToCamera, AtmosphericParam.LightDirection)));
    float intensity = h * h * h * atmosphereBrightness;

    return intensity;
}

float GetAccumulatedInscatteringValue(float InTravelDist, float3 InToPixelNormalized, float2 InUV, int2 InScreenPixelPos)
{
    float Depth = DepthTexture.SampleLevel(DepthTextureSamplerState, InUV, 0).x;

    float dt = 1.0 / AtmosphericParam.TravelCount;
    float dw = 2.0 * (1.0 - AtmosphericParam.SlopeOfDist) * dt;

    float t = dt;
    if (AtmosphericParam.UseNoise != 0)
    {
        float seed = InScreenPixelPos.x* InScreenPixelPos.y / AtmosphericParam.CameraFar;
        t += dt * rand(float2(seed, seed)) * 3.0;
    }
    float tmax = t + 1.0;

    // Start and end Depth to march ray in NDC
    float z1 = TransformNDC(AtmosphericParam.CameraPos + InToPixelNormalized * AtmosphericParam.CameraNear).z;
    float z2 = TransformNDC(AtmosphericParam.CameraPos + InToPixelNormalized * InTravelDist).z;

    // Start and end pos in shadowmap texture space
    float3 p1 = TransformShdowMapTextureSpace(AtmosphericParam.CameraPos + AtmosphericParam.CameraNear);
    float3 p2 = TransformShdowMapTextureSpace(AtmosphericParam.CameraPos + InToPixelNormalized * InTravelDist);

    float weight = AtmosphericParam.SlopeOfDist;	// first weight is always SlopeOfDist

    float AccumulatedValue = 0.0;
    for (; t <= tmax; t += dt)
    {
        float u = (t * (1.0 - AtmosphericParam.SlopeOfDist) + AtmosphericParam.SlopeOfDist) * t;
        float z = lerp(z1, z2, u);

        // When the z meet Depth on screen, stop raymarching
        if (z > Depth)
            break;

        AccumulatedValue += 1.0;

        float3 CurrentPosInShadowMapTS = lerp(p1, p2, u);
        float ShadowMapDepth = ShadowMapTexture.SampleLevel(ShadowMapSamplerState, CurrentPosInShadowMapTS.xy, 0).x;
        if (ShadowMapDepth > CurrentPosInShadowMapTS.z)
            AccumulatedValue += weight;

        weight += dw;
    }

    return AccumulatedValue;
}

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= AtmosphericParam.RTWidth || GlobalInvocationID.y >= AtmosphericParam.RTHeight)
        return;

    float2 uv = GlobalInvocationID.xy / float2(AtmosphericParam.RTWidth - 1, AtmosphericParam.RTHeight - 1);

    float3 WorldPos = GBuffer0.SampleLevel(GBuffer0SamplerState, uv, 0).xyz;

    float3 ToPixel = (WorldPos - AtmosphericParam.CameraPos);
    float TravelDist = sqrt(dot(ToPixel, ToPixel));
    float3 ToPixelNormalized = normalize(ToPixel);

    float AccumulatedValue = GetAccumulatedInscatteringValue(TravelDist, ToPixelNormalized, uv, GlobalInvocationID.xy);
    AccumulatedValue *= AnisotropyIntensity(-ToPixelNormalized);

    Result[GlobalInvocationID.xy].x = AccumulatedValue;
}
