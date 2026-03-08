#include "common.hlsl"

struct SurfelData
{
    float4 PositionRadius;
    float4 NormalSeenFrame;
    float4 AlbedoWeight;
    float4 Extra;
};

struct SurfelIrradianceData
{
    float4 IrradianceAndCount;
    float4 MSMEData0;
    float4 MSMEData1;
};

struct SurfelActiveCounter
{
    uint Count;
    uint3 Padding;
};

struct SurfelGIInlineRayGatherUniformBuffer
{
    float4x4 V;
    float4x4 P;
    float4x4 InvP;
    float4x4 InvV;
    uint MaxSurfels;
    uint RayCount;
    float MaxRayDistance;
    float NormalBias;
    float HistoryBlend;
    float HitDepthThickness;
    int FrameNumber;
    float Padding0;
    float4 SkyColor;
    float4 SunDirectionAndIntensity;
    float4 SunColor;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<uint> ActiveSurfelIndexBuffer : register(t1, space0);
StructuredBuffer<SurfelActiveCounter> ActiveSurfelCounterBuffer : register(t2, space0);
StructuredBuffer<SurfelData> SurfelPool : register(t3, space0);
Texture2D DepthTexture : register(t4, space0);
SamplerState DepthTextureSampler : register(s4, space0);
Texture2D GBufferNormalTexture : register(t5, space0);
SamplerState GBufferNormalSampler : register(s5, space0);
Texture2D GBufferAlbedoTexture : register(t6, space0);
SamplerState GBufferAlbedoSampler : register(s6, space0);
RWStructuredBuffer<SurfelIrradianceData> SurfelIrradianceBuffer : register(u7, space0);

cbuffer GatherUniform : register(b8, space0)
{
    SurfelGIInlineRayGatherUniformBuffer Gather;
}

uint InitGatherSeed(uint surfelIndex, uint frameNumber)
{
    uint seed = surfelIndex * 747796405u + frameNumber * 2891336453u + 277803737u;
    RandomHash(seed);
    return seed;
}

float3 EvaluateMissRadiance(float3 worldDir)
{
    const float3 sunDir = normalize(-Gather.SunDirectionAndIntensity.xyz);
    const float sunNoL = saturate(dot(worldDir, sunDir));
    const float sunTerm = sunNoL * Gather.SunDirectionAndIntensity.w;
    return Gather.SkyColor.xyz + Gather.SunColor.xyz * sunTerm;
}

bool TryProjectToScreen(float3 viewPos, out float2 outUV)
{
    const float4 clipPos = mul(Gather.P, float4(viewPos, 1.0));
    if (abs(clipPos.w) < 1e-6)
        return false;

    float2 uv = clipPos.xy / clipPos.w;
    uv = uv * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return false;

    outUV = uv;
    return true;
}

float3 EvaluateHitRadiance(float3 receiverWorldPos, float3 hitWorldPos, float hitRayT, float3 fallbackColor)
{
    //return float3(1, 0, 0);
    const float distFalloff = 1.0 - smoothstep(0.0, Gather.MaxRayDistance, hitRayT);
    const float3 rayDir = normalize(hitWorldPos - receiverWorldPos);
    const float3 fallbackRadiance = EvaluateMissRadiance(rayDir) * (0.25 + 0.35 * distFalloff) + fallbackColor * 0.03;
    const float3 hitViewPos = mul(Gather.V, float4(hitWorldPos, 1.0)).xyz;

    float2 hitUV = 0.0;
    if (!TryProjectToScreen(hitViewPos, hitUV))
    {
        return fallbackRadiance;
    }

    const float3 sampledHitViewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, hitUV, Gather.InvP);
    if (abs(sampledHitViewPos.z - hitViewPos.z) > max(Gather.HitDepthThickness, hitRayT * 0.1))
    {
        return fallbackRadiance;
    }

    const float3 sampledHitWorldPos = mul(Gather.InvV, float4(sampledHitViewPos, 1.0)).xyz;
    const float3 sampledHitNormal = normalize(GBufferNormalTexture.SampleLevel(GBufferNormalSampler, hitUV, 0).xyz * 2.0 - 1.0);
    const float3 sampledHitAlbedo = GBufferAlbedoTexture.SampleLevel(GBufferAlbedoSampler, hitUV, 0).xyz;
    const float3 hitToReceiver = normalize(receiverWorldPos - sampledHitWorldPos);

    // Use two-sided visibility here to avoid collapsing to 0 when normal orientation is inconsistent.
    const float visibility = max(0.15, abs(dot(sampledHitNormal, hitToReceiver)));
    const float bounce = visibility * (0.25 + 0.75 * distFalloff);
    return max(sampledHitAlbedo, fallbackColor * 0.25) * bounce;
}

struct MSMEState
{
    float3 Mean;
    float3 ShortMean;
    float VBBR;
    float3 Variance;
    float Inconsistency;
};

float ComputeMSMEShortWindowBlend(uint sampleCount, float historyBlend)
{
    const float baseBlend = (1.0 - saturate(historyBlend)) * (max((float)sampleCount, 1.0) / 4.0);
    return clamp(baseBlend, 0.01, 0.10);
}

MSMEState RunMSME(float3 y, MSMEState dataIn, float shortWindowBlend)
{
    MSMEState data = dataIn;

    const float3 dev = sqrt(max(float3(1e-5, 1e-5, 1e-5), data.Variance));
    const float3 highThreshold = float3(0.1, 0.1, 0.1) + data.ShortMean + dev * 8.0;
    const float3 yClamped = min(y, highThreshold);

    const float3 delta = yClamped - data.ShortMean;
    data.ShortMean = lerp(data.ShortMean, yClamped, shortWindowBlend);
    const float3 delta2 = yClamped - data.ShortMean;

    const float varianceBlend = shortWindowBlend * 0.5;
    data.Variance = lerp(data.Variance, delta * delta2, varianceBlend);
    data.Variance = max(data.Variance, 0.0);

    const float3 devNew = sqrt(max(float3(1e-5, 1e-5, 1e-5), data.Variance));
    const float3 shortDiff = data.Mean - data.ShortMean;
    const float relativeDiff = dot(float3(0.299, 0.587, 0.114), abs(shortDiff) / max(float3(1e-5, 1e-5, 1e-5), devNew));
    data.Inconsistency = lerp(data.Inconsistency, relativeDiff, 0.08);

    const float3 term = (0.5 * data.ShortMean) / max(float3(1e-5, 1e-5, 1e-5), devNew);
    const float varianceBasedBlendReduction = clamp(dot(float3(0.299, 0.587, 0.114), term), 1.0 / 32.0, 1.0);

    const float catchUpFactor = smoothstep(0.0, 1.0, relativeDiff * max(0.02, data.Inconsistency - 0.2));
    float catchUpBlend = clamp(catchUpFactor, 1.0 / 256.0, 1.0);
    catchUpBlend *= data.VBBR;
    data.VBBR = lerp(data.VBBR, varianceBasedBlendReduction, 0.1);

    data.Mean = lerp(data.Mean, yClamped, saturate(catchUpBlend));
    return data;
}

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint activeSurfelLinearIndex = GlobalInvocationID.x;
    const uint activeSurfelCount = ActiveSurfelCounterBuffer[0].Count;
    if (activeSurfelLinearIndex >= activeSurfelCount)
        return;

    const uint surfelIndex = ActiveSurfelIndexBuffer[activeSurfelLinearIndex];
    if (surfelIndex >= max(Gather.MaxSurfels, 1u))
        return;

    const SurfelData surfel = SurfelPool[surfelIndex];

    float3 normal = surfel.NormalSeenFrame.xyz;
    const float normalLenSq = dot(normal, normal);
    if (normalLenSq <= 1e-6)
        return;
    normal *= rsqrt(normalLenSq);

    const uint rayCount = max(Gather.RayCount, 1u);
    const float originBias = max(Gather.NormalBias, 0.001);
    const float tMin = max(originBias * 0.25, 0.001);
    const float tMax = max(Gather.MaxRayDistance, tMin + 0.001);
    const float3 rayOrigin = surfel.PositionRadius.xyz + normal * originBias;
    const float3 fallbackColor = max(surfel.AlbedoWeight.xyz, float3(0.03, 0.03, 0.03));

    uint seed = InitGatherSeed(surfelIndex, (uint)max(Gather.FrameNumber, 0));
    float3 accumulatedLi = 0.0;

    [loop] for (uint rayIndex = 0u; rayIndex < rayCount; ++rayIndex)
    {
        const float3 localDir = CosWeightedSampleHemisphere(seed);
        const float3 worldDir = normalize(ToWorld(normal, localDir));

        RayDesc rayDesc;
        rayDesc.Origin = rayOrigin;
        rayDesc.Direction = worldDir;
        rayDesc.TMin = tMin;
        rayDesc.TMax = tMax;

        RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> rayQuery;
        rayQuery.TraceRayInline(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, rayDesc);
        while (rayQuery.Proceed())
        {
        }

        const uint committedStatus = rayQuery.CommittedStatus();
        if (committedStatus == COMMITTED_NOTHING)
        {
            accumulatedLi += EvaluateMissRadiance(worldDir);
        }
        else if (committedStatus == COMMITTED_TRIANGLE_HIT)
        {
            const float hitRayT = rayQuery.CommittedRayT();
            const float3 hitWorldPos = rayOrigin + worldDir * hitRayT;
            accumulatedLi += EvaluateHitRadiance(surfel.PositionRadius.xyz, hitWorldPos, hitRayT, fallbackColor);
        }
    }

    const float3 currentIrradiance = PI * (accumulatedLi / (float)rayCount);
    const SurfelIrradianceData prev = SurfelIrradianceBuffer[surfelIndex];
    MSMEState state;
    state.Mean = prev.IrradianceAndCount.xyz;
    state.ShortMean = prev.MSMEData0.xyz;
    state.VBBR = clamp(prev.MSMEData0.w, 1.0 / 32.0, 1.0);
    state.Variance = max(prev.MSMEData1.xyz, 0.0);
    state.Inconsistency = clamp(prev.MSMEData1.w, 0.0, 10.0);

    const float prevCount = max(prev.IrradianceAndCount.w, 0.0);
    const float shortWindowBlend = ComputeMSMEShortWindowBlend(rayCount, Gather.HistoryBlend);
    if (prevCount < 32.0)
    {
        const float blend = 1.0 / (1.0 + prevCount);
        state.Mean = lerp(state.Mean, currentIrradiance, blend);
        state.ShortMean = lerp(state.ShortMean, currentIrradiance, blend);
        state.Variance = lerp(state.Variance, float3(1.0, 1.0, 1.0), blend);
        state.VBBR = max(state.VBBR, 1.0);
        state.Inconsistency = max(state.Inconsistency, 1.0);
    }
    else
    {
        state = RunMSME(currentIrradiance, state, shortWindowBlend);
    }

    SurfelIrradianceData outData;
    outData.IrradianceAndCount = float4(state.Mean, min(prevCount + 1.0, 200.0));
    outData.MSMEData0 = float4(state.ShortMean, state.VBBR);
    outData.MSMEData1 = float4(state.Variance, state.Inconsistency);
    SurfelIrradianceBuffer[surfelIndex] = outData;
}
