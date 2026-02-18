#include "common.hlsl"

struct VisualizeUniformBuffer
{
    float4x4 InvP;
    float4x4 InvV;
    float2 ScreenSize;
    float BlendAlpha;
    float GridCellSize;
    float Cascade1Scale;
    int MaxSurfels;
    int NeighborCellRadius;
    int BlendWithScene;
    int ShowStateDebug;
    int SurfelsPerCell;
    int ShowCellDebug;
    int ShowCellGrid;
    int Padding0;
    int Padding1;
};

struct SurfelData
{
    float4 PositionRadius;
    float4 NormalSeenFrame;
    float4 AlbedoWeight;
    float4 Extra;
};

RWTexture2D<float4> Result : register(u0, space0);
Texture2D DepthTexture : register(t1, space0);
SamplerState DepthTextureSampler : register(s1, space0);
Texture2D LinearDepthTexture : register(t2, space0);
StructuredBuffer<SurfelData> SurfelPool : register(t3, space0);

cbuffer VisualizeCommon : register(b4, space0)
{
    VisualizeUniformBuffer VisualizeCommon;
}

uint HashU32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

uint Hash3(int3 v)
{
    uint h = 2166136261u;
    h = (h ^ asuint(v.x)) * 16777619u;
    h = (h ^ asuint(v.y)) * 16777619u;
    h = (h ^ asuint(v.z)) * 16777619u;
    return HashU32(h);
}

float3 CellDebugColor(int3 cellCoord, uint cascadeIndex)
{
    uint h = Hash3(cellCoord);
    h ^= HashU32(cascadeIndex * 0x9e3779b9u);
    h = HashU32(h);
    const float r = (float)(h & 255u) / 255.0;
    const float g = (float)((h >> 8u) & 255u) / 255.0;
    const float b = (float)((h >> 16u) & 255u) / 255.0;
    return saturate(float3(0.2, 0.2, 0.2) + float3(r, g, b) * 0.8);
}

float3 RadiusDebugColor(float radius)
{
    // Log-compress wide radius range, then map small->large as Blue->Cyan->Green->Yellow->Red.
    const float t = saturate((log2(max(radius, 0.0001) + 1.0) - 1.0) / 6.0);
    const float4 c0 = float4(0.12, 0.35, 1.00, 0.0);
    const float4 c1 = float4(0.12, 0.90, 1.00, 0.25);
    const float4 c2 = float4(0.15, 1.00, 0.30, 0.5);
    const float4 c3 = float4(1.00, 0.95, 0.15, 0.75);
    const float4 c4 = float4(1.00, 0.20, 0.15, 1.0);

    if (t < c1.w)
        return lerp(c0.xyz, c1.xyz, saturate(t / max(c1.w - c0.w, 0.0001)));
    if (t < c2.w)
        return lerp(c1.xyz, c2.xyz, saturate((t - c1.w) / max(c2.w - c1.w, 0.0001)));
    if (t < c3.w)
        return lerp(c2.xyz, c3.xyz, saturate((t - c2.w) / max(c3.w - c2.w, 0.0001)));
    return lerp(c3.xyz, c4.xyz, saturate((t - c3.w) / max(c4.w - c3.w, 0.0001)));
}

uint GetSlotsPerCell(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint clampedDesired = clamp(desiredSlotsPerCell, 1u, 8u);
    return min(max(1u, maxSurfels), clampedDesired);
}

uint GetCellCount(uint maxSurfels, uint desiredSlotsPerCell)
{
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    return max(1u, maxSurfels / slotsPerCell);
}

uint HashCellWithCascade(int3 cellCoord, uint cascadeIndex)
{
    uint h = Hash3(cellCoord);
    h ^= HashU32(cascadeIndex * 0x9e3779b9u);
    return HashU32(h);
}

uint GetCellBaseIndex(int3 cellCoord, uint maxSurfels, uint desiredSlotsPerCell, uint cascadeIndex)
{
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    const uint cellCount = GetCellCount(maxSurfels, desiredSlotsPerCell);
    const uint cellHash = HashCellWithCascade(cellCoord, cascadeIndex) % cellCount;
    return cellHash * slotsPerCell;
}

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const int2 pixel = int2(GlobalInvocationID.xy);
    const int2 screenSize = int2(VisualizeCommon.ScreenSize);
    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    const float2 uv = (float2(pixel) + 0.5) / float2(screenSize);
    const float rawDepth = DepthTexture.SampleLevel(DepthTextureSampler, uv, 0).x;
    if (rawDepth <= 0.0)
    {
        Result[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float3 viewPos = CalcViewPositionFromDepth(DepthTexture, DepthTextureSampler, uv, VisualizeCommon.InvP);
    const float3 worldPos = mul(VisualizeCommon.InvV, float4(viewPos, 1.0)).xyz;

    const float cascade0CellSize = max(VisualizeCommon.GridCellSize, 0.1);
    const uint cascadeIndex = 0u;
    const float cellSize = cascade0CellSize;

    const uint maxSurfels = max((uint)VisualizeCommon.MaxSurfels, 1u);
    const uint desiredSlotsPerCell = max((uint)VisualizeCommon.SurfelsPerCell, 1u);
    const uint slotsPerCell = GetSlotsPerCell(maxSurfels, desiredSlotsPerCell);
    const int3 baseCellCoord = int3(floor(worldPos / cellSize));
    const int neighborRadius = clamp(VisualizeCommon.NeighborCellRadius, 0, 3);

    float bestDist2 = 1e38;
    float3 bestColor = float3(0.0, 0.0, 0.0);

    [loop] for (int z = -neighborRadius; z <= neighborRadius; ++z)
    {
        [loop] for (int y = -neighborRadius; y <= neighborRadius; ++y)
        {
            [loop] for (int x = -neighborRadius; x <= neighborRadius; ++x)
            {
                const int3 queryCellCoord = baseCellCoord + int3(x, y, z);
                const uint baseIndex = GetCellBaseIndex(queryCellCoord, maxSurfels, desiredSlotsPerCell, cascadeIndex);
                [loop] for (uint slot = 0u; slot < slotsPerCell; ++slot)
                {
                    const uint surfelIndex = baseIndex + slot;
                    const SurfelData s = SurfelPool[surfelIndex];
                    const bool isDormant = (s.Extra.y < 0.5) && (abs(s.Extra.x - 5.0) < 0.5);
                    if (s.Extra.y < 0.5 && !isDormant)
                        continue;
                    if (isDormant && VisualizeCommon.ShowStateDebug == 0)
                        continue;

                    const uint surfelCascade = (uint)round(s.Extra.w);
                    if (surfelCascade != cascadeIndex)
                        continue;

                    const float3 surfelPos = s.PositionRadius.xyz;
                    const int3 surfelCellCoord = int3(floor(surfelPos / cellSize));
                    if (any(surfelCellCoord != queryCellCoord))
                        continue;

                    const float surfelRadius = max(s.PositionRadius.w, 0.0001);
                    const float3 delta = worldPos - surfelPos;
                    const float d2 = dot(delta, delta);
                    const float r2 = surfelRadius * surfelRadius;

                    if (d2 <= r2 && d2 < bestDist2)
                    {
                        bestDist2 = d2;
                        const float d = sqrt(max(d2, 0.0));
                        if (VisualizeCommon.ShowCellDebug != 0)
                        {
                            bestColor = CellDebugColor(surfelCellCoord, surfelCascade);
                        }
                        else
                        {
                            bestColor = RadiusDebugColor(surfelRadius);
                        }

                        if (VisualizeCommon.ShowStateDebug != 0 && VisualizeCommon.ShowCellDebug == 0)
                        {
                            const float state = s.Extra.x;
                            float3 stateTint = float3(1.0, 1.0, 1.0);
                            // 0:new spawn, 1:stable keep, 2:merged moved, 3:replaced far, 4:stolen far, 5:dormant, 6:revived dormant.
                            if (state < 0.5)
                                stateTint = float3(1.0, 0.2, 0.2);
                            else if (state < 1.5)
                                stateTint = float3(0.2, 1.0, 0.2);
                            else if (state < 2.5)
                                stateTint = float3(1.0, 0.95, 0.2);
                            else if (state < 3.5)
                                stateTint = float3(0.2, 1.0, 1.0);
                            else if (state < 4.5)
                                stateTint = float3(1.0, 0.2, 1.0);
                            else if (state < 5.5)
                                stateTint = float3(0.7, 0.7, 0.7);
                            else
                                stateTint = float3(0.2, 0.6, 1.0);

                            bestColor = stateTint;
                        }
                    }
                }
            }
        }
    }

    float3 visualizeColor;
    const bool hasSurfel = (bestDist2 < 1e37);
    if (hasSurfel)
    {
        visualizeColor = bestColor;
    }
    else
    {
        const float sceneDepth = LinearDepthTexture.Load(int3(pixel, 0)).x;
        const float v = saturate(sceneDepth * 0.02);
        visualizeColor = float3(v * 0.08, v * 0.08, v * 0.08);
    }

    if (VisualizeCommon.ShowCellGrid != 0)
    {
        const float3 gridCoord = worldPos / max(cellSize, 0.001);
        const float3 fracCoord = frac(gridCoord);
        const float3 edgeDist = min(fracCoord, 1.0 - fracCoord);
        const float nearestEdge = min(edgeDist.x, min(edgeDist.y, edgeDist.z));
        const float lineWidth = 0.02;
        const float lineMask = 1.0 - smoothstep(lineWidth, lineWidth * 2.5, nearestEdge);
        visualizeColor = lerp(visualizeColor, float3(1.0, 1.0, 1.0), lineMask * 0.85);
    }

    Result[pixel] = float4(visualizeColor, 1.0);
}
