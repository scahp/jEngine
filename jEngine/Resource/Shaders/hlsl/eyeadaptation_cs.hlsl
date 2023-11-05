// This code is based on UE5 EyeAdaptation Basic Method

#define TILE_SIZE 16

struct jEyeAdaptationUniformBuffer
{
    float2 ViewportMin;
    float2 ViewportMax;
    float MinLuminanceAverage;
    float MaxLuminanceAverage;
    float DeltaFrametime;
    float AdaptationSpeed;
    float ExposureCompensation;
};

Texture2D SceneColor : register(t0);
Texture2D EyeAdaptationTexture : register(t1);
RWTexture2D<float4> RWEyeAdaptationTexture : register(u2);

groupshared float2 SubRectTotalValueWeight[TILE_SIZE * TILE_SIZE];

cbuffer EyeAdaptation : register(b3)
{
    jEyeAdaptationUniformBuffer EyeAdaptation;
}

float2 ComputeWeightedSubRegion(uint2 InSubRectMin, uint2 InSubRectMax, float2 InViewportMin, float2 InViewportMax)
{
    float2 LogLuminanceAndWeight = float2(0.0, 0.0);

    if (((InSubRectMax.x - InSubRectMin.x) == 0) || ((InSubRectMax.y - InSubRectMin.y) == 0))
    {
        return float2(0.0, 0.0000001f);
    }

    const float2 InvViewportSize = 1.0f / (InViewportMax - InViewportMin);

    for (uint i = InSubRectMin.x; i < InSubRectMax.x; ++i)
    {
        for (uint j = InSubRectMin.y; j < InSubRectMax.y; ++j)
        {
            float2 UV = float2(float(i) * InvViewportSize.x, float(j) * InvViewportSize.y);

            LogLuminanceAndWeight.x += SceneColor.Load(int3(i, j, 0)).w;
        }
    }

    LogLuminanceAndWeight.y = (InSubRectMax.y - InSubRectMin.y) * (InSubRectMax.x - InSubRectMin.x);

    return LogLuminanceAndWeight;
}

float ComputeEyeAdaptation(float InLuminanceAverageOld, float InLuminanceAverage, float InDeltaFrametime)
{
    const float LogTarget = log2(InLuminanceAverage);
    const float LogOld = log2(InLuminanceAverageOld);

    // linear blend
    const float Offset = InDeltaFrametime * EyeAdaptation.AdaptationSpeed;
    const float LogAdaptedLuminanceAverage = (LogTarget > LogOld) ? min(LogTarget, LogOld + Offset) : max(LogTarget, LogOld - Offset);
    const float AdaptedLuminanceAverage = exp2(LogAdaptedLuminanceAverage);

    return AdaptedLuminanceAverage;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void main(uint GIndex : SV_GroupIndex, uint2 GTId : SV_GroupThreadID)
{
    const uint2 SubRectMin = uint2((EyeAdaptation.ViewportMax.xy - EyeAdaptation.ViewportMin.xy) * GTId.xy / TILE_SIZE);
    const uint2 SubRectMax = uint2((EyeAdaptation.ViewportMax.xy - EyeAdaptation.ViewportMin.xy) * (GTId.xy + uint2(1, 1)) / TILE_SIZE);

    const float2 LogLuminanceAverageWeight = ComputeWeightedSubRegion(SubRectMin, SubRectMax, EyeAdaptation.ViewportMin, EyeAdaptation.ViewportMax);

    SubRectTotalValueWeight[GIndex] = LogLuminanceAverageWeight;
    GroupMemoryBarrierWithGroupSync();

    // Merge all value from all threads
    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 1];
    GroupMemoryBarrierWithGroupSync();

    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 2];
    GroupMemoryBarrierWithGroupSync();

    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 4];
    GroupMemoryBarrierWithGroupSync();

    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 8];
    GroupMemoryBarrierWithGroupSync();

    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 16];
    GroupMemoryBarrierWithGroupSync();

    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 32];
    GroupMemoryBarrierWithGroupSync();

    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 64];
    GroupMemoryBarrierWithGroupSync();

    SubRectTotalValueWeight[GIndex] += SubRectTotalValueWeight[GIndex ^ 128];
    GroupMemoryBarrierWithGroupSync();

    float LogLuminanceAverage = SubRectTotalValueWeight[0].x / SubRectTotalValueWeight[0].y;
    float LuminanceAverage = exp2(LogLuminanceAverage);
    LuminanceAverage = clamp(LuminanceAverage, EyeAdaptation.MinLuminanceAverage, EyeAdaptation.MaxLuminanceAverage);

    const float KeyValue = 0.18 * EyeAdaptation.ExposureCompensation;        // Middle grey

    const float ExposureScaleOld = EyeAdaptationTexture.Load(int3(0, 0, 0)).x;
    const float LuminanceAverageOld = KeyValue / (ExposureScaleOld != 0.0 ? ExposureScaleOld : 1.0);

    // the exposure changes over time
    float AdaptedLuminance = ComputeEyeAdaptation(LuminanceAverageOld, LuminanceAverage, EyeAdaptation.DeltaFrametime);

    // This range check commented to make smooth transition for the KeyValue.
    //  - If the KeyValue is changed in this frame, the KeyValue is not matched with the KeyValue that applied ExposureScaleOld.
    //    So the ApdatedLuminance range will not be in Min or Max Luminance average. and I commented it.
    // AdaptedLuminance = clamp(AdaptedLuminance, EyeAdaptation.MinLuminanceAverage, EyeAdaptation.MaxLuminanceAverage);

    const float AdaptedExposureScale = KeyValue / clamp(AdaptedLuminance, 0.0001f, 10000.0f);

    if (GIndex == 0)
    {
        RWEyeAdaptationTexture[uint2(0, 0)].x = AdaptedExposureScale;
    }
}
