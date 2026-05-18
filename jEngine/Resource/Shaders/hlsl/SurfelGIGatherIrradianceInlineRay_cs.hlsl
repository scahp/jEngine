#include "common.hlsl"

struct SurfelData
{
    float4 PositionRadius;
    float3 Normal;
    uint LastSeenFrame;
    float4 AlbedoWeight;
    int State;
    uint IsActive;
    uint OwnerCellHash;
    uint CascadeIndex;
};

struct SurfelIrradianceData
{
    float4 IrradianceAndCount;
    float4 MSMEData0;
    float4 MSMEData1;
};

#define SURFEL_GI_GUIDE_DIM 4
#define SURFEL_GI_GUIDE_LOBE_COUNT (SURFEL_GI_GUIDE_DIM * SURFEL_GI_GUIDE_DIM)
#define SURFEL_GI_GUIDE_TOTAL_FLOATS (SURFEL_GI_GUIDE_LOBE_COUNT + SURFEL_GI_GUIDE_DIM)
#define SURFEL_GI_GUIDE_LEARNING_RATE 0.02
#define SURFEL_GI_GUIDE_MAX_BLEND 0.9

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
    uint UseGuiding;
    float HitDepthThickness;
    uint FrameNumber;
    float Padding0;
    float4 SkyColor;
    float4 SunDirectionAndIntensity;
    float4 SunColor;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<uint> ActiveSurfelIndexBuffer : register(t1, space0);
StructuredBuffer<uint> ActiveSurfelCounterBuffer : register(t2, space0);
StructuredBuffer<SurfelData> SurfelPool : register(t3, space0);
Texture2D DepthTexture : register(t4, space0);
SamplerState DepthTextureSampler : register(s4, space0);
Texture2D GBufferNormalTexture : register(t5, space0);
SamplerState GBufferNormalSampler : register(s5, space0);
Texture2D GBufferAlbedoTexture : register(t6, space0);
SamplerState GBufferAlbedoSampler : register(s6, space0);
RWStructuredBuffer<SurfelIrradianceData> SurfelIrradianceBuffer : register(u7, space0);
RWStructuredBuffer<float> SurfelGuidingBuffer : register(u8, space0);

cbuffer GatherUniform : register(b9, space0)
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

struct GuidedSample
{
    float3 LocalDir;
    float2 UV;
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

uint GetGuidingBaseIndex(uint surfelIndex)
{
    return surfelIndex * SURFEL_GI_GUIDE_TOTAL_FLOATS;
}

float GetGuidingTotalMass(uint surfelIndex)
{
    const uint baseIndex = GetGuidingBaseIndex(surfelIndex) + SURFEL_GI_GUIDE_LOBE_COUNT;
    float total = 0.0;
    [unroll] for (uint row = 0u; row < SURFEL_GI_GUIDE_DIM; ++row)
    {
        total += max(0.0, SurfelGuidingBuffer[baseIndex + row]);
    }
    return total;
}

float GetGuidingPDF(uint surfelIndex, float2 uv, float totalMass)
{
    if (totalMass <= 1e-6)
        return 0.0;

    const uint cellX = min((uint)floor(uv.x * SURFEL_GI_GUIDE_DIM), (uint)(SURFEL_GI_GUIDE_DIM - 1));
    const uint cellY = min((uint)floor(uv.y * SURFEL_GI_GUIDE_DIM), (uint)(SURFEL_GI_GUIDE_DIM - 1));
    const uint cellIndex = cellY * SURFEL_GI_GUIDE_DIM + cellX;
    const uint baseIndex = GetGuidingBaseIndex(surfelIndex);
    const float weight = max(0.0, SurfelGuidingBuffer[baseIndex + cellIndex]);
    const float jacobian = HemiOctSquareJacobian(uv);
    return (weight / totalMass) * ((float)SURFEL_GI_GUIDE_LOBE_COUNT / jacobian);
}

int SampleGuidingLobeIndex(uint surfelIndex, float u, float totalMass)
{
    if (totalMass <= 1e-6)
        return -1;

    const uint baseIndex = GetGuidingBaseIndex(surfelIndex);
    const uint rowSumBaseIndex = baseIndex + SURFEL_GI_GUIDE_LOBE_COUNT;
    float target = clamp(u * totalMass, 0.0, totalMass);
    uint selectedRow = 0u;

    [unroll] for (uint row = 0u; row < SURFEL_GI_GUIDE_DIM; ++row)
    {
        const float weight = max(0.0, SurfelGuidingBuffer[rowSumBaseIndex + row]);
        if (target <= weight || row == (uint)(SURFEL_GI_GUIDE_DIM - 1))
        {
            selectedRow = row;
            break;
        }
        target -= weight;
    }

    const uint rowBaseIndex = baseIndex + selectedRow * SURFEL_GI_GUIDE_DIM;
    uint selectedCol = 0u;
    [unroll] for (uint col = 0u; col < SURFEL_GI_GUIDE_DIM; ++col)
    {
        const float weight = max(0.0, SurfelGuidingBuffer[rowBaseIndex + col]);
        if (target <= weight || col == (uint)(SURFEL_GI_GUIDE_DIM - 1))
        {
            selectedCol = col;
            break;
        }
        target -= weight;
    }

    return (int)(selectedRow * SURFEL_GI_GUIDE_DIM + selectedCol);
}

float UpdateGuidingFromSample(uint surfelIndex, float2 uv, float luminance)
{
    const float2 gridPos = uv * SURFEL_GI_GUIDE_DIM - 0.5;
    const float2 basePos = floor(gridPos);
    const float2 fraction = frac(gridPos);
    const uint baseIndex = GetGuidingBaseIndex(surfelIndex);
    const uint rowSumBaseIndex = baseIndex + SURFEL_GI_GUIDE_LOBE_COUNT;
    float massDiff = 0.0;

    [unroll] for (int y = 0; y <= 1; ++y)
    {
        [unroll] for (int x = 0; x <= 1; ++x)
        {
            const int cellX = (int)basePos.x + x;
            const int cellY = (int)basePos.y + y;
            if (cellX < 0 || cellX >= SURFEL_GI_GUIDE_DIM || cellY < 0 || cellY >= SURFEL_GI_GUIDE_DIM)
                continue;

            const float weightX = (x == 0) ? (1.0 - fraction.x) : fraction.x;
            const float weightY = (y == 0) ? (1.0 - fraction.y) : fraction.y;
            const float target = luminance * weightX * weightY;
            const uint cellIndex = (uint)cellY * SURFEL_GI_GUIDE_DIM + (uint)cellX;
            const float oldValue = SurfelGuidingBuffer[baseIndex + cellIndex];
            const float newValue = lerp(oldValue, target, SURFEL_GI_GUIDE_LEARNING_RATE);
            SurfelGuidingBuffer[baseIndex + cellIndex] = newValue;

            const float diff = newValue - oldValue;
            SurfelGuidingBuffer[rowSumBaseIndex + (uint)cellY] += diff;
            massDiff += diff;
        }
    }

    return massDiff;
}

GuidedSample SampleGuidedDirection(uint surfelIndex, float totalMass, float guideBlend, inout uint seed)
{
    const float4 randoms = float4(SafeU01(Random_0_1(seed)), SafeU01(Random_0_1(seed)), SafeU01(Random_0_1(seed)), SafeU01(Random_0_1(seed)));
    GuidedSample result;

    if (totalMass > 1e-6 && guideBlend > 0.0 && randoms.w < guideBlend)
    {
        const int lobeIndex = SampleGuidingLobeIndex(surfelIndex, randoms.z, totalMass);
        if (lobeIndex >= 0)
        {
            const uint col = (uint)lobeIndex % SURFEL_GI_GUIDE_DIM;
            const uint row = (uint)lobeIndex / SURFEL_GI_GUIDE_DIM;
            result.UV = float2(((float)col + randoms.x) / SURFEL_GI_GUIDE_DIM, ((float)row + randoms.y) / SURFEL_GI_GUIDE_DIM);
            result.LocalDir = HemiOctSquareDecode(result.UV);
            return result;
        }
    }

    result.LocalDir = CosWeightedSampleHemisphereFromUniform(randoms.xy);
    result.UV = HemiOctSquareEncode(result.LocalDir);
    return result;
}

[numthreads(64, 1, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const uint activeSurfelLinearIndex = GlobalInvocationID.x;
    const uint activeSurfelCount = ActiveSurfelCounterBuffer[0];
    if (activeSurfelLinearIndex >= activeSurfelCount)
        return;

    const uint surfelIndex = ActiveSurfelIndexBuffer[activeSurfelLinearIndex];
    if (surfelIndex >= max(Gather.MaxSurfels, 1u))
        return;

    const SurfelData surfel = SurfelPool[surfelIndex];

    float3 normal = surfel.Normal;
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

    const SurfelIrradianceData prev = SurfelIrradianceBuffer[surfelIndex];
    const float prevCount = max(prev.IrradianceAndCount.w, 0.0);
    float guidingMass = GetGuidingTotalMass(surfelIndex);
    const float guideRamp = saturate(prevCount / 16.0);
    const float guideBlend = (Gather.UseGuiding != 0 && guidingMass > 1e-5) ? min(SURFEL_GI_GUIDE_MAX_BLEND * guideRamp, SURFEL_GI_GUIDE_MAX_BLEND) : 0.0;

    uint seed = InitGatherSeed(surfelIndex, Gather.FrameNumber);
    float3 accumulatedIrradiance = 0.0;

    [loop] for (uint rayIndex = 0u; rayIndex < rayCount; ++rayIndex)
    {
        const GuidedSample guidedSample = SampleGuidedDirection(surfelIndex, guidingMass, guideBlend, seed);
        const float3 localDir = guidedSample.LocalDir;
        const float2 guideUV = guidedSample.UV;
        const float cosTerm = max(0.0, localDir.z);
        if (cosTerm <= 1e-6)
            continue;

        const float pdfCos = cosTerm / PI;
        const float pdfGuide = (guideBlend > 0.0) ? GetGuidingPDF(surfelIndex, guideUV, guidingMass) : 0.0;
        const float mixPdf = lerp(pdfCos, pdfGuide, guideBlend);
        if (mixPdf <= 1e-6)
            continue;

        const float3 worldDir = normalize(ToWorld(normal, localDir));
        float3 sampleLi = 0.0;

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
            sampleLi = EvaluateMissRadiance(worldDir);
        }
        else if (committedStatus == COMMITTED_TRIANGLE_HIT)
        {
            const float hitRayT = rayQuery.CommittedRayT();
            const float3 hitWorldPos = rayOrigin + worldDir * hitRayT;
            sampleLi = EvaluateHitRadiance(surfel.PositionRadius.xyz, hitWorldPos, hitRayT, fallbackColor);
        }

        accumulatedIrradiance += sampleLi * (cosTerm / mixPdf);
        const float sampleLuminance = dot(sampleLi, float3(0.2126, 0.7152, 0.0722)) * cosTerm;
        guidingMass = max(guidingMass + UpdateGuidingFromSample(surfelIndex, guideUV, sampleLuminance), 0.0);
    }

    const float3 currentIrradiance = accumulatedIrradiance / (float)rayCount;
    MSMEState state;
    state.Mean = prev.IrradianceAndCount.xyz;
    state.ShortMean = prev.MSMEData0.xyz;
    state.VBBR = clamp(prev.MSMEData0.w, 1.0 / 32.0, 1.0);
    state.Variance = max(prev.MSMEData1.xyz, 0.0);
    state.Inconsistency = clamp(prev.MSMEData1.w, 0.0, 10.0);

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
