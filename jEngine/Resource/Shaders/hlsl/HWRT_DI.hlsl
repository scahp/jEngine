#include "common.hlsl"
#include "PBR.hlsl"
#include "lightutil.hlsl"

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float3 cameraPosition;
    float normalBias;
    uint numLights;
    uint debugViewMode;
    uint forceMipLevel0;
    uint renderWidth;
    float debugLineWidth;
    float debugUVScale;
    float debugPrimitiveIDScale;
    float shadowRayStartOffset;
    uint renderHeight;
    float padding0;
    float padding1;
    float padding2;
};

struct MaterialInstanceUniform
{
    uint materialFlags;
    uint albedoSamplerIndex;
    uint normalSamplerIndex;
    uint rmSamplerIndex;
    float alphaCutoff;
    float3 padding0;
};

static const uint HWRTDI_MaterialFlag_HasAlbedoTexture = 1u << 0;
static const uint HWRTDI_MaterialFlag_HasNormalTexture = 1u << 1;
static const uint HWRTDI_MaterialFlag_HasRMTexture = 1u << 2;
static const uint HWRTDI_MaterialFlag_UseSRGBAlbedoTexture = 1u << 3;
static const uint HWRTDI_MaterialFlag_IsSkyMaterial = 1u << 4;
static const uint HWRTDI_MaterialFlag_UseAlphaCutout = 1u << 5;
static const uint HWRTDI_MaterialFlag_NonOpaqueGeometry = 1u << 6;
static const uint HWRTDI_RAY_MASK_SCENE = 0x01u;

bool HasMaterialFlag(in MaterialInstanceUniform MaterialInstance, in uint Flag)
{
    return (MaterialInstance.materialFlags & Flag) != 0;
}

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u1, space0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b2, space0);
SamplerState DefaultSamplerState : register(s3, space0);
TextureCube<float4> EnvTexture : register(t4, space0);

StructuredBuffer<uint2> VertexIndexOffsetArray[] : register(t0, space1);
Buffer<uint> IndexBindlessArray[] : register(t0, space2);
StructuredBuffer<RenderObjectUniformBuffer> RenderObjParamArray[] : register(t0, space3);
ByteAddressBuffer VerticesBindlessArray[] : register(t0, space4);
ConstantBuffer<MaterialInstanceUniform> MaterialInstanceArray[] : register(b0, space5);
Texture2D AlbedoTextureArray[] : register(t0, space6);
Texture2D NormalTextureArray[] : register(t0, space7);
Texture2D RMTextureArray[] : register(t0, space8);
SamplerState AlbedoSamplerArray[] : register(s0, space9);
SamplerState NormalSamplerArray[] : register(s0, space10);
SamplerState RMSamplerArray[] : register(s0, space11);

struct HWRTDILightData
{
    float4 ColorAndType;               // rgb: color, w: ELightType (1: directional, 2: point, 3: spot)
    float4 PositionAndMaxDistance;     // xyz: position, w: max distance
    float4 DirectionAndPenumbra;       // xyz: direction, w: penumbra radian
    float4 UmbraAndPadding;            // x: umbra radian
};

StructuredBuffer<HWRTDILightData> LightBuffer : register(t5, space0);

static const uint HWRTDI_LightType_Directional = 1u;
static const uint HWRTDI_LightType_Point = 2u;
static const uint HWRTDI_LightType_Spot = 3u;

typedef BuiltInTriangleIntersectionAttributes MyAttributes;

struct RayPayload
{
    float3 Radiance;
    uint Visibility;
};

struct SurfaceData
{
    float3 WorldPos;
    float3 WorldNormal;
    float3 GeometricWorldNormal;
    float2 UV;
    float3 Albedo;
    float3 AlbedoTextureSample;
    float3 NormalTextureSample;
    float3 RMTextureSample;
    float Metallic;
    float Roughness;
    float Alpha;
    float UVStretchMetric;
    float AlbedoMipLevel;
    float NormalMipLevel;
    float RMMipLevel;
    uint MaterialFlags;
    uint PrimitiveIdx;
};

float3 HashPrimitiveColor(in uint PrimitiveIdx)
{
    const float Seed = (float)(PrimitiveIdx + 1u) * g_sceneCB.debugPrimitiveIDScale;
    const float3 Color = frac(float3(0.1031, 0.11369, 0.13787) * Seed);
    return 0.2 + 0.8 * Color;
}

float3 EvaluateDebugViewColor(in SurfaceData Surface, in MyAttributes Attr)
{
    const uint Mode = g_sceneCB.debugViewMode;
    const float B1 = saturate(Attr.barycentrics.x);
    const float B2 = saturate(Attr.barycentrics.y);
    const float B0 = saturate(1.0 - B1 - B2);

    if (Mode == 1u)
    {
        const float EdgeDist = min(B0, min(B1, B2));
        const float LineWidth = max(g_sceneCB.debugLineWidth, 1e-5);
        const float EdgeMask = 1.0 - smoothstep(0.0, LineWidth, EdgeDist);
        return lerp(float3(0.02, 0.02, 0.02), float3(1.0, 1.0, 1.0), EdgeMask);
    }
    if (Mode == 2u)
    {
        return float3(frac(Surface.UV), 0.0);
    }
    if (Mode == 3u)
    {
        const float2 GridUV = Surface.UV * max(g_sceneCB.debugUVScale, 1.0);
        const float2 Cell = floor(GridUV);
        const float Checker = fmod(Cell.x + Cell.y, 2.0);
        const float3 BaseColor = lerp(float3(0.08, 0.08, 0.08), float3(0.85, 0.85, 0.85), Checker);

        const float2 LocalUV = frac(GridUV);
        const float GridEdgeDist = min(min(LocalUV.x, 1.0 - LocalUV.x), min(LocalUV.y, 1.0 - LocalUV.y));
        const float GridLineWidth = max(g_sceneCB.debugLineWidth * 2.0, 0.001);
        const float GridLine = 1.0 - smoothstep(0.0, GridLineWidth, GridEdgeDist);
        return lerp(BaseColor, float3(1.0, 0.2, 0.2), GridLine);
    }
    if (Mode == 4u)
    {
        return HashPrimitiveColor(Surface.PrimitiveIdx);
    }
    if (Mode == 5u)
    {
        return float3(B0, B1, B2);
    }
    if (Mode == 6u)
    {
        const float Stretch = max(Surface.UVStretchMetric, 1.0);
        const float Heat = saturate((Stretch - 1.0) / 8.0);
        return lerp(float3(0.1, 0.9, 0.2), float3(1.0, 0.1, 0.1), Heat);
    }
    if (Mode == 7u)
    {
        return normalize(Surface.WorldNormal) * 0.5 + 0.5;
    }
    if (Mode == 8u)
    {
        return normalize(Surface.GeometricWorldNormal) * 0.5 + 0.5;
    }
    if (Mode == 9u)
    {
        return Surface.AlbedoTextureSample;
    }
    if (Mode == 10u)
    {
        return Surface.NormalTextureSample;
    }
    if (Mode == 11u)
    {
        return Surface.RMTextureSample;
    }
    if (Mode == 12u)
    {
        const float Mip = max(Surface.AlbedoMipLevel, max(Surface.NormalMipLevel, Surface.RMMipLevel));
        const float Heat = saturate(Mip / 8.0);
        return lerp(float3(0.1, 0.2, 1.0), float3(1.0, 0.2, 0.1), Heat);
    }
    if (Mode == 13u)
    {
        const bool IsNonOpaque = (Surface.MaterialFlags & HWRTDI_MaterialFlag_NonOpaqueGeometry) != 0;
        return IsNonOpaque ? float3(1.0, 0.2, 0.2) : float3(0.2, 1.0, 0.2);
    }

    return 0.0;
}

float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

void GetGeometryResources(
    in uint InstanceIdx,
    out StructuredBuffer<uint2> VertexIndexOffset,
    out Buffer<uint> IndexBuffer,
    out StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam,
    out ByteAddressBuffer VertexBuffer,
    out MaterialInstanceUniform MaterialInstance)
{
    VertexIndexOffset = VertexIndexOffsetArray[InstanceIdx];
    IndexBuffer = IndexBindlessArray[InstanceIdx];
    RenderObjParam = RenderObjParamArray[InstanceIdx];
    VertexBuffer = VerticesBindlessArray[InstanceIdx];
    MaterialInstance = MaterialInstanceArray[InstanceIdx];
}

void GetTriangleVertices(
    in uint InstanceIdx,
    in uint PrimitiveIdx,
    out jVertex Vertex0,
    out jVertex Vertex1,
    out jVertex Vertex2,
    out RenderObjectUniformBuffer RenderObjParam,
    out MaterialInstanceUniform MaterialInstance)
{
    StructuredBuffer<uint2> VertexIndexOffset;
    Buffer<uint> IndexBuffer;
    StructuredBuffer<RenderObjectUniformBuffer> RenderObjParamBuffer;
    ByteAddressBuffer VertexBuffer;
    GetGeometryResources(InstanceIdx, VertexIndexOffset, IndexBuffer, RenderObjParamBuffer, VertexBuffer, MaterialInstance);

    const uint VertexOffset = VertexIndexOffset[0].x;
    const uint IndexOffset = VertexIndexOffset[0].y;

    const uint3 Indices = uint3(
        IndexBuffer[IndexOffset + PrimitiveIdx * 3 + 0],
        IndexBuffer[IndexOffset + PrimitiveIdx * 3 + 1],
        IndexBuffer[IndexOffset + PrimitiveIdx * 3 + 2]);

    Vertex0 = GetVertex(VertexBuffer, Indices.x + VertexOffset);
    Vertex1 = GetVertex(VertexBuffer, Indices.y + VertexOffset);
    Vertex2 = GetVertex(VertexBuffer, Indices.z + VertexOffset);
    RenderObjParam = RenderObjParamBuffer[0];
}

float ComputeApproxTextureMipLevel(
    in Texture2D Texture,
    in float3 WorldPos,
    in jVertex Vertex0,
    in jVertex Vertex1,
    in jVertex Vertex2,
    in RenderObjectUniformBuffer RenderObjParam)
{
    uint Width = 1;
    uint Height = 1;
    uint MipLevels = 1;
    Texture.GetDimensions(0, Width, Height, MipLevels);

    const float2 UVEdge0 = Vertex1.TexCoord - Vertex0.TexCoord;
    const float2 UVEdge1 = Vertex2.TexCoord - Vertex0.TexCoord;
    const float UVArea = abs(UVEdge0.x * UVEdge1.y - UVEdge0.y * UVEdge1.x);
    if (UVArea < 1e-8)
        return 0.0;

    const float3 WorldPos0 = mul(RenderObjParam.M, float4(Vertex0.Pos, 1.0)).xyz;
    const float3 WorldPos1 = mul(RenderObjParam.M, float4(Vertex1.Pos, 1.0)).xyz;
    const float3 WorldPos2 = mul(RenderObjParam.M, float4(Vertex2.Pos, 1.0)).xyz;
    const float WorldArea = length(cross(WorldPos1 - WorldPos0, WorldPos2 - WorldPos0));
    if (WorldArea < 1e-8)
        return 0.0;

    const float TexelCount = max((float)Width * (float)Height, 1.0);
    const float TexelWorldArea = WorldArea / max(UVArea * TexelCount, 1e-8);

    const float DistanceToCamera = max(length(WorldPos - g_sceneCB.cameraPosition), 1e-3);
    const float PixelWorldRadius = DistanceToCamera / max((float)g_sceneCB.renderHeight, 1.0);
    const float PixelWorldArea = PixelWorldRadius * PixelWorldRadius;
    const float FootprintTexels = PixelWorldArea / max(TexelWorldArea, 1e-8);

    const float MipLevel = 0.5 * log2(max(FootprintTexels, 1.0));
    return clamp(MipLevel, 0.0, max((float)MipLevels - 1.0, 0.0));
}

float ComputeUVStretchMetric(
    in jVertex Vertex0,
    in jVertex Vertex1,
    in jVertex Vertex2,
    in RenderObjectUniformBuffer RenderObjParam)
{
    const float2 UVEdge0 = Vertex1.TexCoord - Vertex0.TexCoord;
    const float2 UVEdge1 = Vertex2.TexCoord - Vertex0.TexCoord;

    const float3 WorldPos0 = mul(RenderObjParam.M, float4(Vertex0.Pos, 1.0)).xyz;
    const float3 WorldPos1 = mul(RenderObjParam.M, float4(Vertex1.Pos, 1.0)).xyz;
    const float3 WorldPos2 = mul(RenderObjParam.M, float4(Vertex2.Pos, 1.0)).xyz;
    const float3 WorldEdge0 = WorldPos1 - WorldPos0;
    const float3 WorldEdge1 = WorldPos2 - WorldPos0;

    const float UVLen0 = max(length(UVEdge0), 1e-8);
    const float UVLen1 = max(length(UVEdge1), 1e-8);
    const float WorldLen0 = max(length(WorldEdge0), 1e-8);
    const float WorldLen1 = max(length(WorldEdge1), 1e-8);

    const float Scale0 = UVLen0 / WorldLen0;
    const float Scale1 = UVLen1 / WorldLen1;
    const float MinScale = max(min(Scale0, Scale1), 1e-8);
    const float MaxScale = max(Scale0, Scale1);
    return MaxScale / MinScale;
}

float4 SampleAlbedoTexture(in uint InstanceIdx, in MaterialInstanceUniform MaterialInstance, in float2 UV, in float MipLevel)
{
    if (!HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasAlbedoTexture))
        return float4(1.0, 1.0, 1.0, 1.0);

    float4 AlbedoSample = AlbedoTextureArray[InstanceIdx].SampleLevel(AlbedoSamplerArray[MaterialInstance.albedoSamplerIndex], UV, MipLevel);
    if (HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_UseSRGBAlbedoTexture))
    {
        AlbedoSample.rgb = pow(AlbedoSample.rgb, 2.2);
    }
    return AlbedoSample;
}

float3x3 GetNormalMatrix(in RenderObjectUniformBuffer RenderObjParam)
{
    return transpose((float3x3)RenderObjParam.InvM);
}

float3 ComputeFaceWorldNormal(
    in jVertex Vertex0,
    in jVertex Vertex1,
    in jVertex Vertex2,
    in RenderObjectUniformBuffer RenderObjParam)
{
    const float3 WorldPos0 = mul(RenderObjParam.M, float4(Vertex0.Pos, 1.0)).xyz;
    const float3 WorldPos1 = mul(RenderObjParam.M, float4(Vertex1.Pos, 1.0)).xyz;
    const float3 WorldPos2 = mul(RenderObjParam.M, float4(Vertex2.Pos, 1.0)).xyz;
    const float3 FaceNormal = cross(WorldPos1 - WorldPos0, WorldPos2 - WorldPos0);
    const float FaceLength = length(FaceNormal);
    if (FaceLength < 1e-8)
        return float3(0.0, 1.0, 0.0);
    return FaceNormal / FaceLength;
}

float3 GetShadingNormal(
    in uint InstanceIdx,
    in MaterialInstanceUniform MaterialInstance,
    in float2 UV,
    in jVertex Vertex0,
    in jVertex Vertex1,
    in jVertex Vertex2,
    in MyAttributes Attr,
    in RenderObjectUniformBuffer RenderObjParam,
    in float MipLevel)
{
    const float3x3 NormalMatrix = GetNormalMatrix(RenderObjParam);
    float3 LocalNormal = normalize(HitAttribute(Vertex0.Normal, Vertex1.Normal, Vertex2.Normal, Attr));
    float3 WorldNormal = normalize(mul(NormalMatrix, LocalNormal));

    if (!HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasNormalTexture))
        return WorldNormal;

    float3 LocalTangent = normalize(HitAttribute(Vertex0.Tangent, Vertex1.Tangent, Vertex2.Tangent, Attr));
    float3 LocalBitangent = normalize(HitAttribute(Vertex0.Bitangent, Vertex1.Bitangent, Vertex2.Bitangent, Attr));
    if (dot(LocalBitangent, LocalBitangent) < 0.5)
    {
        LocalBitangent = normalize(cross(LocalNormal, LocalTangent));
    }
    const float Handedness = (dot(cross(LocalNormal, LocalTangent), LocalBitangent) < 0.0) ? -1.0 : 1.0;

    float3 WorldTangent = normalize(mul((float3x3)RenderObjParam.M, LocalTangent));
    WorldTangent = normalize(WorldTangent - WorldNormal * dot(WorldNormal, WorldTangent));
    float3 WorldBitangent = normalize(cross(WorldNormal, WorldTangent)) * Handedness;

    float3 TangentSpaceNormal = NormalTextureArray[InstanceIdx].SampleLevel(NormalSamplerArray[MaterialInstance.normalSamplerIndex], UV, MipLevel).xyz;
    TangentSpaceNormal.y = 1.0 - TangentSpaceNormal.y;
    TangentSpaceNormal = TangentSpaceNormal * 2.0 - 1.0;

    // Match the raster GBuffer path: worldNormal = mul(transpose(float3x3(T, B, N)), tangentSpaceNormal)
    const float3x3 TBN = transpose(float3x3(WorldTangent, WorldBitangent, WorldNormal));
    return normalize(mul(TBN, TangentSpaceNormal));
}

MyAttributes MakeAttributesFromBarycentrics(in float2 Barycentrics)
{
    MyAttributes Attr;
    Attr.barycentrics = Barycentrics;
    return Attr;
}

SurfaceData GetSurfaceDataInternal(in uint InstanceIdx, in uint PrimitiveIdx, in MyAttributes Attr, in float3 WorldPos)
{
    SurfaceData Surface = (SurfaceData)0;

    jVertex Vertex0;
    jVertex Vertex1;
    jVertex Vertex2;
    RenderObjectUniformBuffer RenderObjParam;
    MaterialInstanceUniform MaterialInstance;
    GetTriangleVertices(InstanceIdx, PrimitiveIdx, Vertex0, Vertex1, Vertex2, RenderObjParam, MaterialInstance);

    Surface.WorldPos = WorldPos;
    Surface.PrimitiveIdx = PrimitiveIdx;
    Surface.UV = HitAttribute(Vertex0.TexCoord, Vertex1.TexCoord, Vertex2.TexCoord, Attr);
    Surface.MaterialFlags = MaterialInstance.materialFlags;
    Surface.GeometricWorldNormal = ComputeFaceWorldNormal(Vertex0, Vertex1, Vertex2, RenderObjParam);
    Surface.AlbedoTextureSample = float3(1.0, 1.0, 1.0);
    Surface.NormalTextureSample = float3(0.5, 0.5, 1.0);
    Surface.RMTextureSample = float3(0.0, RenderObjParam.Roughness, RenderObjParam.Metallic);
    Surface.AlbedoMipLevel = 0.0;
    Surface.NormalMipLevel = 0.0;
    Surface.RMMipLevel = 0.0;
    Surface.UVStretchMetric = ComputeUVStretchMetric(Vertex0, Vertex1, Vertex2, RenderObjParam);

    const bool ForceMipLevel0 = (g_sceneCB.forceMipLevel0 != 0u);
    float AlbedoMipLevel = 0.0;
    if (HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasAlbedoTexture))
    {
        AlbedoMipLevel = ComputeApproxTextureMipLevel(AlbedoTextureArray[InstanceIdx], Surface.WorldPos, Vertex0, Vertex1, Vertex2, RenderObjParam);
    }
    if (ForceMipLevel0)
    {
        AlbedoMipLevel = 0.0;
    }
    Surface.AlbedoMipLevel = AlbedoMipLevel;
    const float4 AlbedoSample = SampleAlbedoTexture(InstanceIdx, MaterialInstance, Surface.UV, AlbedoMipLevel);
    Surface.Albedo = AlbedoSample.rgb;
    Surface.AlbedoTextureSample = AlbedoSample.rgb;
    Surface.Alpha = AlbedoSample.a;
    float NormalMipLevel = 0.0;
    if (HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasNormalTexture))
    {
        NormalMipLevel = ComputeApproxTextureMipLevel(NormalTextureArray[InstanceIdx], Surface.WorldPos, Vertex0, Vertex1, Vertex2, RenderObjParam);
    }
    if (ForceMipLevel0)
    {
        NormalMipLevel = 0.0;
    }
    Surface.NormalMipLevel = NormalMipLevel;
    if (HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasNormalTexture))
    {
        Surface.NormalTextureSample = NormalTextureArray[InstanceIdx].SampleLevel(NormalSamplerArray[MaterialInstance.normalSamplerIndex], Surface.UV, NormalMipLevel).xyz;
    }
    Surface.WorldNormal = GetShadingNormal(InstanceIdx, MaterialInstance, Surface.UV, Vertex0, Vertex1, Vertex2, Attr, RenderObjParam, NormalMipLevel);
    Surface.Metallic = RenderObjParam.Metallic;
    Surface.Roughness = RenderObjParam.Roughness;

    if (HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasRMTexture))
    {
        float RMMipLevel = ComputeApproxTextureMipLevel(RMTextureArray[InstanceIdx], Surface.WorldPos, Vertex0, Vertex1, Vertex2, RenderObjParam);
        if (ForceMipLevel0)
        {
            RMMipLevel = 0.0;
        }
        Surface.RMMipLevel = RMMipLevel;
        const float4 RMSample = RMTextureArray[InstanceIdx].SampleLevel(RMSamplerArray[MaterialInstance.rmSamplerIndex], Surface.UV, RMMipLevel);
        Surface.Roughness = RMSample.y;
        Surface.Metallic = RMSample.z;
        Surface.RMTextureSample = RMSample.rgb;
    }

    Surface.Roughness = clamp(Surface.Roughness, 0.04, 1.0);
    Surface.Metallic = saturate(Surface.Metallic);
    return Surface;
}

SurfaceData GetSurfaceData(in uint InstanceIdx, in MyAttributes Attr)
{
    return GetSurfaceDataInternal(InstanceIdx, PrimitiveIndex(), Attr, HitWorldPosition());
}

bool IsRayHitRejectedByMaterial(in uint InstanceIdx, in uint PrimitiveIdx, in float2 Barycentrics)
{
    jVertex Vertex0;
    jVertex Vertex1;
    jVertex Vertex2;
    RenderObjectUniformBuffer RenderObjParam;
    MaterialInstanceUniform MaterialInstance;
    GetTriangleVertices(InstanceIdx, PrimitiveIdx, Vertex0, Vertex1, Vertex2, RenderObjParam, MaterialInstance);

    if (HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasAlbedoTexture)
        && HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_UseAlphaCutout))
    {
        const MyAttributes Attr = MakeAttributesFromBarycentrics(Barycentrics);
        const float2 UV = HitAttribute(Vertex0.TexCoord, Vertex1.TexCoord, Vertex2.TexCoord, Attr);
        const float Alpha = SampleAlbedoTexture(InstanceIdx, MaterialInstance, UV, 0.0).a;
        if (Alpha < saturate(MaterialInstance.alphaCutoff))
        {
            return true;
        }
    }

    return false;
}

bool RequiresShadowMaterialRejectTest(in uint InstanceIdx)
{
    const MaterialInstanceUniform MaterialInstance = MaterialInstanceArray[InstanceIdx];

    const bool UseAlphaCutout = HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_HasAlbedoTexture)
        && HasMaterialFlag(MaterialInstance, HWRTDI_MaterialFlag_UseAlphaCutout);
    return UseAlphaCutout;
}

bool TraceShadowRay(in float3 Origin, in float3 Direction, in float TMax)
{
    const float RayStartOffset = max(g_sceneCB.shadowRayStartOffset, 0.0);
    const float TMin = max(RayStartOffset, 1e-5);

    RayDesc Ray;
    Ray.Origin = Origin + Direction * RayStartOffset;
    Ray.Direction = Direction;
    Ray.TMin = TMin;
    Ray.TMax = max(TMax, Ray.TMin);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> ShadowRayQuery;
    ShadowRayQuery.TraceRayInline(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, HWRTDI_RAY_MASK_SCENE, Ray);
    while (ShadowRayQuery.Proceed())
    {
        if (ShadowRayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            const uint CandidateInstanceIdx = ShadowRayQuery.CandidateInstanceIndex();
            const uint CandidatePrimitiveIdx = ShadowRayQuery.CandidatePrimitiveIndex();
            if (RequiresShadowMaterialRejectTest(CandidateInstanceIdx))
            {
                const float2 CandidateBarycentrics = ShadowRayQuery.CandidateTriangleBarycentrics();
                if (IsRayHitRejectedByMaterial(CandidateInstanceIdx, CandidatePrimitiveIdx, CandidateBarycentrics))
                    continue;
            }

            ShadowRayQuery.CommitNonOpaqueTriangleHit();
        }
    }

    return (ShadowRayQuery.CommittedStatus() == COMMITTED_NOTHING);
}

float3 EvaluateUnifiedLights(in SurfaceData Surface, in float3 ViewDir)
{
    float3 Result = 0.0;
    const uint NumLights = g_sceneCB.numLights;
    for (uint i = 0; i < NumLights; ++i)
    {
        const HWRTDILightData Light = LightBuffer[i];
        const uint LightType = (uint)round(Light.ColorAndType.w);
        const float3 LightColor = Light.ColorAndType.xyz;

        if (LightType == HWRTDI_LightType_Directional)
        {
            const float3 L = normalize(-Light.DirectionAndPenumbra.xyz);
            const float NdotL = saturate(dot(Surface.WorldNormal, L));
            if (NdotL <= 0.0)
                continue;

            const float3 ShadowOrigin = Surface.WorldPos + Surface.WorldNormal * g_sceneCB.normalBias;
            if (!TraceShadowRay(ShadowOrigin, L, 100000.0))
                continue;

            Result += PBR(L, Surface.WorldNormal, ViewDir, Surface.Albedo, LightColor, 1.0, Surface.Metallic, Surface.Roughness);
        }
        else if (LightType == HWRTDI_LightType_Point)
        {
            float3 ToLight = Light.PositionAndMaxDistance.xyz - Surface.WorldPos;
            const float DistanceToLight = length(ToLight);
            const float MaxDistance = max(Light.PositionAndMaxDistance.w, 0.001);
            if (DistanceToLight <= 0.001 || DistanceToLight > MaxDistance)
                continue;

            const float3 L = ToLight / DistanceToLight;
            const float NdotL = saturate(dot(Surface.WorldNormal, L));
            if (NdotL <= 0.0)
                continue;

            const float3 ShadowOrigin = Surface.WorldPos + Surface.WorldNormal * g_sceneCB.normalBias;
            if (!TraceShadowRay(ShadowOrigin, L, DistanceToLight - g_sceneCB.normalBias))
                continue;

            const float Attenuation = DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0 / MaxDistance);
            Result += PBR(L, Surface.WorldNormal, ViewDir, Surface.Albedo, LightColor, DistanceToLight, Surface.Metallic, Surface.Roughness) * Attenuation;
        }
        else if (LightType == HWRTDI_LightType_Spot)
        {
            float3 ToLight = Light.PositionAndMaxDistance.xyz - Surface.WorldPos;
            const float DistanceToLight = length(ToLight);
            const float MaxDistance = max(Light.PositionAndMaxDistance.w, 0.001);
            if (DistanceToLight <= 0.001 || DistanceToLight > MaxDistance)
                continue;

            const float3 L = ToLight / DistanceToLight;
            const float NdotL = saturate(dot(Surface.WorldNormal, L));
            if (NdotL <= 0.0)
                continue;

            const float Penumbra = Light.DirectionAndPenumbra.w;
            const float Umbra = Light.UmbraAndPadding.x;
            const float LightRadian = acos(saturate(dot(L, -Light.DirectionAndPenumbra.xyz)));
            const float Attenuation = DistanceAttenuation2(DistanceToLight * DistanceToLight, 1.0 / MaxDistance)
                * DiretionalFalloff(LightRadian, Penumbra, Umbra);
            if (Attenuation <= 0.0)
                continue;

            const float3 ShadowOrigin = Surface.WorldPos + Surface.WorldNormal * g_sceneCB.normalBias;
            if (!TraceShadowRay(ShadowOrigin, L, DistanceToLight - g_sceneCB.normalBias))
                continue;

            Result += PBR(L, Surface.WorldNormal, ViewDir, Surface.Albedo, LightColor, DistanceToLight, Surface.Metallic, Surface.Roughness) * Attenuation;
        }
    }
    return Result;
}

void GenerateCameraRay(in uint2 Index, in uint2 RenderDimensions, out float3 Origin, out float3 Direction)
{
    float2 ScreenPos = ((float2)Index + 0.5f) / (float2)RenderDimensions * 2.0 - 1.0;
    ScreenPos.y = -ScreenPos.y;

    float4 World = mul(g_sceneCB.projectionToWorld, float4(ScreenPos, 0.0, 1.0));
    World.xyz /= World.w;

    Origin = g_sceneCB.cameraPosition;
    Direction = normalize(World.xyz - Origin);
}

float3 EvaluateSurfaceRadianceFromViewPosition(in SurfaceData Surface, in MyAttributes Attr, in float3 ViewPosition)
{
    if (g_sceneCB.debugViewMode != 0u)
    {
        return EvaluateDebugViewColor(Surface, Attr);
    }

    const float3 ViewDir = normalize(ViewPosition - Surface.WorldPos);

    float3 Radiance = 0.0;
    Radiance += EvaluateUnifiedLights(Surface, ViewDir);
    return Radiance;
}

float3 EvaluateSurfaceRadiance(in SurfaceData Surface, in MyAttributes Attr)
{
    return EvaluateSurfaceRadianceFromViewPosition(Surface, Attr, g_sceneCB.cameraPosition);
}

[numthreads(8, 8, 1)]
void InlineRayQueryCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint2 Pixel = DispatchThreadID.xy;
    if (Pixel.x >= g_sceneCB.renderWidth || Pixel.y >= g_sceneCB.renderHeight)
        return;

    float3 Origin;
    float3 Direction;
    GenerateCameraRay(Pixel, uint2(g_sceneCB.renderWidth, g_sceneCB.renderHeight), Origin, Direction);

    RayDesc Ray;
    Ray.Origin = Origin;
    Ray.Direction = Direction;
    Ray.TMin = 0.001;
    Ray.TMax = 100000.0;

    RayQuery<RAY_FLAG_NONE> PrimaryRayQuery;
    PrimaryRayQuery.TraceRayInline(Scene, RAY_FLAG_NONE, HWRTDI_RAY_MASK_SCENE, Ray);
    while (PrimaryRayQuery.Proceed())
    {
        if (PrimaryRayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            const uint CandidateInstanceIdx = PrimaryRayQuery.CandidateInstanceIndex();
            const uint CandidatePrimitiveIdx = PrimaryRayQuery.CandidatePrimitiveIndex();
            const float2 CandidateBarycentrics = PrimaryRayQuery.CandidateTriangleBarycentrics();

            if (IsRayHitRejectedByMaterial(CandidateInstanceIdx, CandidatePrimitiveIdx, CandidateBarycentrics))
                continue;

            PrimaryRayQuery.CommitNonOpaqueTriangleHit();
        }
    }

    float3 Radiance = 0.0;
    if (PrimaryRayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        const uint InstanceIdx = PrimaryRayQuery.CommittedInstanceIndex();
        const uint PrimitiveIdx = PrimaryRayQuery.CommittedPrimitiveIndex();
        const float2 Barycentrics = PrimaryRayQuery.CommittedTriangleBarycentrics();
        const MyAttributes Attr = MakeAttributesFromBarycentrics(Barycentrics);
        const float HitT = PrimaryRayQuery.CommittedRayT();
        const float3 WorldPos = Origin + Direction * HitT;
        const SurfaceData Surface = GetSurfaceDataInternal(InstanceIdx, PrimitiveIdx, Attr, WorldPos);
        Radiance = EvaluateSurfaceRadiance(Surface, Attr);
    }

    RenderTarget[Pixel] = float4(Radiance, 1.0);
}

[shader("raygeneration")]
void RaygenShader()
{
    float3 Origin;
    float3 Direction;
    GenerateCameraRay(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, Origin, Direction);

    RayDesc Ray;
    Ray.Origin = Origin;
    Ray.Direction = Direction;
    Ray.TMin = 0.001;
    Ray.TMax = 100000.0;

    RayPayload Payload = (RayPayload)0;
    TraceRay(Scene, RAY_FLAG_NONE, HWRTDI_RAY_MASK_SCENE, 0, 0, 0, Ray, Payload);
    RenderTarget[DispatchRaysIndex().xy] = float4(Payload.Radiance, 1.0);
}

[shader("anyhit")]
void PrimaryAnyHitShader(inout RayPayload Payload, in MyAttributes Attr)
{
    if (IsRayHitRejectedByMaterial(InstanceIndex(), PrimitiveIndex(), Attr.barycentrics))
    {
        IgnoreHit();
        return;
    }
}

[shader("closesthit")]
void PrimaryClosestHitShader(inout RayPayload Payload, in MyAttributes Attr)
{
    const SurfaceData Surface = GetSurfaceData(InstanceIndex(), Attr);
    Payload.Radiance = EvaluateSurfaceRadiance(Surface, Attr);
}

[shader("anyhit")]
void ShadowAnyHitShader(inout RayPayload Payload, in MyAttributes Attr)
{
    PrimaryAnyHitShader(Payload, Attr);
}

[shader("closesthit")]
void ShadowClosestHitShader(inout RayPayload Payload, in MyAttributes Attr)
{
    Payload.Visibility = 0;
}

[shader("miss")]
void PrimaryMissShader(inout RayPayload Payload)
{
    Payload.Radiance = 0.0;
}

[shader("miss")]
void ShadowMissShader(inout RayPayload Payload)
{
    Payload.Visibility = 1;
}

struct SurfelGIData
{
    float4 PositionRadius;
    float4 NormalSeenFrame;
    float4 AlbedoWeight;
    float4 Extra;
};

struct SurfelGIIrradianceData
{
    float4 IrradianceAndCount;
    float4 MSMEData0;
    float4 MSMEData1;
};

struct SurfelGIActiveCounter
{
    uint Count;
    uint3 Padding;
};

struct SurfelGIGatherUniformBuffer
{
    uint MaxSurfels;
    uint RayCount;
    float MaxRayDistance;
    float NormalBias;
    float HistoryBlend;
    int FrameNumber;
    uint Padding0;
    uint Padding1;
};

StructuredBuffer<uint> SurfelGIActiveSurfelIndexBuffer : register(t0, space12);
StructuredBuffer<SurfelGIActiveCounter> SurfelGIActiveSurfelCounterBuffer : register(t1, space12);
StructuredBuffer<SurfelGIData> SurfelGIPool : register(t2, space12);
RWStructuredBuffer<SurfelGIIrradianceData> SurfelGIIrradianceBuffer : register(u3, space12);
ConstantBuffer<SurfelGIGatherUniformBuffer> g_surfelGatherCB : register(b4, space12);

uint InitSurfelGatherSeed(uint SurfelIndex, uint FrameNumber)
{
    uint Seed = SurfelIndex * 747796405u + FrameNumber * 2891336453u + 277803737u;
    RandomHash(Seed);
    return Seed;
}

float3 EvaluateSurfelGatherMissRadiance(in float3 WorldDir)
{
    return EnvTexture.SampleLevel(DefaultSamplerState, normalize(WorldDir), 0.0).rgb;
}

struct SurfelGIMSMEState
{
    float3 Mean;
    float3 ShortMean;
    float VBBR;
    float3 Variance;
    float Inconsistency;
};

float ComputeMSMEShortWindowBlend(uint SampleCount, float HistoryBlend)
{
    const float BaseBlend = (1.0 - saturate(HistoryBlend)) * (max((float)SampleCount, 1.0) / 4.0);
    return clamp(BaseBlend, 0.01, 0.10);
}

SurfelGIMSMEState RunMSME(float3 Y, SurfelGIMSMEState DataIn, float ShortWindowBlend)
{
    SurfelGIMSMEState Data = DataIn;

    const float3 Dev = sqrt(max(float3(1e-5, 1e-5, 1e-5), Data.Variance));
    const float3 HighThreshold = float3(0.1, 0.1, 0.1) + Data.ShortMean + Dev * 8.0;
    const float3 YClamped = min(Y, HighThreshold);

    const float3 Delta = YClamped - Data.ShortMean;
    Data.ShortMean = lerp(Data.ShortMean, YClamped, ShortWindowBlend);
    const float3 Delta2 = YClamped - Data.ShortMean;

    const float VarianceBlend = ShortWindowBlend * 0.5;
    Data.Variance = lerp(Data.Variance, Delta * Delta2, VarianceBlend);
    Data.Variance = max(Data.Variance, 0.0);

    const float3 DevNew = sqrt(max(float3(1e-5, 1e-5, 1e-5), Data.Variance));
    const float3 ShortDiff = Data.Mean - Data.ShortMean;
    const float RelativeDiff = dot(float3(0.299, 0.587, 0.114), abs(ShortDiff) / max(float3(1e-5, 1e-5, 1e-5), DevNew));
    Data.Inconsistency = lerp(Data.Inconsistency, RelativeDiff, 0.08);

    const float3 Term = (0.5 * Data.ShortMean) / max(float3(1e-5, 1e-5, 1e-5), DevNew);
    const float VarianceBasedBlendReduction = clamp(dot(float3(0.299, 0.587, 0.114), Term), 1.0 / 32.0, 1.0);

    const float CatchUpFactor = smoothstep(0.0, 1.0, RelativeDiff * max(0.02, Data.Inconsistency - 0.2));
    float CatchUpBlend = clamp(CatchUpFactor, 1.0 / 256.0, 1.0);
    CatchUpBlend *= Data.VBBR;
    Data.VBBR = lerp(Data.VBBR, VarianceBasedBlendReduction, 0.1);

    Data.Mean = lerp(Data.Mean, YClamped, saturate(CatchUpBlend));
    return Data;
}

[numthreads(64, 1, 1)]
void SurfelGIGatherIrradianceHWRT_CS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    const uint ActiveSurfelLinearIndex = DispatchThreadID.x;
    const uint ActiveSurfelCount = SurfelGIActiveSurfelCounterBuffer[0].Count;
    if (ActiveSurfelLinearIndex >= ActiveSurfelCount)
        return;

    const uint SurfelIndex = SurfelGIActiveSurfelIndexBuffer[ActiveSurfelLinearIndex];
    if (SurfelIndex >= max(g_surfelGatherCB.MaxSurfels, 1u))
        return;

    const SurfelGIData Surfel = SurfelGIPool[SurfelIndex];
    float3 ReceiverNormal = Surfel.NormalSeenFrame.xyz;
    const float ReceiverNormalLenSq = dot(ReceiverNormal, ReceiverNormal);
    if (ReceiverNormalLenSq <= 1e-6)
        return;
    ReceiverNormal *= rsqrt(ReceiverNormalLenSq);

    const float3 ReceiverWorldPos = Surfel.PositionRadius.xyz;
    const uint RayCount = max(g_surfelGatherCB.RayCount, 1u);
    const float OriginBias = max(g_surfelGatherCB.NormalBias, 0.001);
    const float TMin = max(OriginBias * 0.25, 0.001);
    const float TMax = max(g_surfelGatherCB.MaxRayDistance, TMin + 0.001);
    const float3 RayOrigin = ReceiverWorldPos + ReceiverNormal * OriginBias;

    uint Seed = InitSurfelGatherSeed(SurfelIndex, (uint)max(g_surfelGatherCB.FrameNumber, 0));
    float3 AccumulatedLi = 0.0;

    [loop] for (uint RayIndex = 0u; RayIndex < RayCount; ++RayIndex)
    {
        const float3 LocalDir = CosWeightedSampleHemisphere(Seed);
        const float3 WorldDir = normalize(ToWorld(ReceiverNormal, LocalDir));

        RayDesc Ray;
        Ray.Origin = RayOrigin;
        Ray.Direction = WorldDir;
        Ray.TMin = TMin;
        Ray.TMax = TMax;

        RayQuery<RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> GatherRayQuery;
        GatherRayQuery.TraceRayInline(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, HWRTDI_RAY_MASK_SCENE, Ray);
        while (GatherRayQuery.Proceed())
        {
            if (GatherRayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
            {
                const uint CandidateInstanceIdx = GatherRayQuery.CandidateInstanceIndex();
                const uint CandidatePrimitiveIdx = GatherRayQuery.CandidatePrimitiveIndex();
                const float2 CandidateBarycentrics = GatherRayQuery.CandidateTriangleBarycentrics();
                if (IsRayHitRejectedByMaterial(CandidateInstanceIdx, CandidatePrimitiveIdx, CandidateBarycentrics))
                    continue;

                GatherRayQuery.CommitNonOpaqueTriangleHit();
            }
        }

        const uint CommittedStatus = GatherRayQuery.CommittedStatus();
        if (CommittedStatus == COMMITTED_TRIANGLE_HIT)
        {
            const uint InstanceIdx = GatherRayQuery.CommittedInstanceIndex();
            const uint PrimitiveIdx = GatherRayQuery.CommittedPrimitiveIndex();
            const float2 Barycentrics = GatherRayQuery.CommittedTriangleBarycentrics();
            const float HitRayT = GatherRayQuery.CommittedRayT();
            const float3 HitWorldPos = RayOrigin + WorldDir * HitRayT;
            const MyAttributes Attr = MakeAttributesFromBarycentrics(Barycentrics);
            const SurfaceData Surface = GetSurfaceDataInternal(InstanceIdx, PrimitiveIdx, Attr, HitWorldPos);
            AccumulatedLi += EvaluateSurfaceRadianceFromViewPosition(Surface, Attr, ReceiverWorldPos);
        }
        else
        {
            AccumulatedLi += EvaluateSurfelGatherMissRadiance(WorldDir);
        }
    }

    const float3 CurrentIrradiance = PI * (AccumulatedLi / (float)RayCount);
    const SurfelGIIrradianceData Prev = SurfelGIIrradianceBuffer[SurfelIndex];
    SurfelGIMSMEState State;
    State.Mean = Prev.IrradianceAndCount.xyz;
    State.ShortMean = Prev.MSMEData0.xyz;
    State.VBBR = clamp(Prev.MSMEData0.w, 1.0 / 32.0, 1.0);
    State.Variance = max(Prev.MSMEData1.xyz, 0.0);
    State.Inconsistency = clamp(Prev.MSMEData1.w, 0.0, 10.0);

    const float PrevCount = max(Prev.IrradianceAndCount.w, 0.0);
    const float ShortWindowBlend = ComputeMSMEShortWindowBlend(RayCount, g_surfelGatherCB.HistoryBlend);
    if (PrevCount < 32.0)
    {
        const float Blend = 1.0 / (1.0 + PrevCount);
        State.Mean = lerp(State.Mean, CurrentIrradiance, Blend);
        State.ShortMean = lerp(State.ShortMean, CurrentIrradiance, Blend);
        State.Variance = lerp(State.Variance, float3(1.0, 1.0, 1.0), Blend);
        State.VBBR = max(State.VBBR, 1.0);
        State.Inconsistency = max(State.Inconsistency, 1.0);
    }
    else
    {
        State = RunMSME(CurrentIrradiance, State, ShortWindowBlend);
    }

    SurfelGIIrradianceData OutData;
    OutData.IrradianceAndCount = float4(State.Mean, min(PrevCount + 1.0, 200.0));
    OutData.MSMEData0 = float4(State.ShortMean, State.VBBR);
    OutData.MSMEData1 = float4(State.Variance, State.Inconsistency);
    SurfelGIIrradianceBuffer[SurfelIndex] = OutData;
}
