#include "common.hlsl"

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float3 cameraPosition;
    float focalDistance;
    float3 cameraDirection;
    float lensRadius;
    uint FrameNumber;
    uint AccumulateNumber;
    float2 HaltonJitter;
};

struct MaterialUniformBuffer
{
    float3 baseColor;
    float anisotropic;

    float3 emission;
    int lightId;

    float metallic;
    float roughness;
    float subsurface;
    float specularTint;

    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;

    float specTrans;
    float ior;
    float mediumType;
    float mediumDensity;

    float3 mediumColor;
    float mediumAnisotropy;

    float baseColorTexId;
    float metallicRoughnessTexID;
    float normalmapTexID;
    float emissionmapTexID;

    float opacity;
    float alphaMode;
    float alphaCutoff;
    float padding2;
};

struct LightUniformBuffer
{
	float3 position;
	float radius;
	float3 emission;
	float area;
	float3 u;
	int type;
	float3 v;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u1, space0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b2, space0);

StructuredBuffer<uint2> VertexIndexOffsetArray[] : register(t0, space1);
StructuredBuffer<uint> IndexBindlessArray[] : register(t0, space2);
StructuredBuffer<RenderObjectUniformBuffer> RenderObjParamArray[] : register(t0, space3);
ByteAddressBuffer VerticesBindlessArray[] : register(t0, space4);
ConstantBuffer<MaterialUniformBuffer> MaterialBindlessArray[] : register(b0, space5);
ConstantBuffer<LightUniformBuffer> LightBindlessArray[] : register(b0, space6);

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline float3 GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    screenPos += g_sceneCB.HaltonJitter.xy; // Apply Jittering from Halton Sequence
    
    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(g_sceneCB.projectionToWorld, float4(screenPos, 0, 1));

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);

    return world.xyz;
}

float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

void GetShaderBindingResources(
    inout StructuredBuffer<uint2> VertexIndexOffsetBuffer,
    inout StructuredBuffer<uint> IndexBuffer,
    inout StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam,
    inout ByteAddressBuffer VerticesBuffer,
    inout ConstantBuffer<MaterialUniformBuffer> MaterialBuffer,
    in uint InstanceIdx)
{
    VertexIndexOffsetBuffer = VertexIndexOffsetArray[InstanceIdx];
    IndexBuffer = IndexBindlessArray[InstanceIdx];
    RenderObjParam = RenderObjParamArray[InstanceIdx];
    VerticesBuffer = VerticesBindlessArray[InstanceIdx];
    MaterialBuffer = MaterialBindlessArray[InstanceIdx];
}

void GetLightShaderBindingResource(inout ConstantBuffer<LightUniformBuffer> LightBuffer, int LightId)
{
    LightBuffer = LightBindlessArray[LightId];
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float3 HitPos;
    float3 HitReflectDir;
    float3 Attenuation;
    float3 Radiance;
    uint seed;
    uint RecursionDepth;
};

// todo : set from shader
#define MAX_RECURSION_DEPTH 10

void SamplingBRDF(out float3 SampleDir, out float SamplePDF, out float3 BRDF_Cos
    , in float3 WorldNormal, in float3 IncidenceDir, in ConstantBuffer<MaterialUniformBuffer> mat, inout uint seed)
{
    // Lambertian surface
    SampleDir = CosWeightedSampleHemisphere(seed);
    float cosine_thea = SampleDir.z;
    SampleDir = ToWorld(WorldNormal, SampleDir);
    
    SamplePDF = (1.0f / PI) * cosine_thea;
    float3 BRDF = (1.0f / PI) * mat.baseColor;

    BRDF_Cos = BRDF * cosine_thea;
}

[shader("raygeneration")]
void RaygenShader()
{
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, g_sceneCB.AccumulateNumber);

    #define MAX_PIXEL_PER_RAY 3
    float3 TotalRadiance = 0.0f;
    
    for (int i = 0; i < MAX_PIXEL_PER_RAY; ++i)
    {
        float3 origin = 0;
        float3 rayDir = 0;
        GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = rayDir;
        ray.TMin = 0.001;
        ray.TMax = 1000.0;

        RayPayload payload;
        payload.RecursionDepth = 0;
        payload.seed = seed;
        payload.HitPos = float3(0, 0, 0);
        payload.HitReflectDir = float3(0, 0, 0);
        payload.Radiance = float3(0, 0, 0);
        payload.Attenuation = float3(1, 1, 1);

        float3 radiance = 0.0f;
        float3 attenuation = 1.0f;

        while (payload.RecursionDepth < MAX_RECURSION_DEPTH)
        {
            TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);

            radiance += attenuation * payload.Radiance;
            attenuation *= payload.Attenuation;

            ray.Origin = payload.HitPos;
            ray.Direction = payload.HitReflectDir;
            ++payload.RecursionDepth;
        }

        TotalRadiance += radiance;
        
        seed = payload.seed;

    }
    TotalRadiance /= MAX_PIXEL_PER_RAY;
    
    if (g_sceneCB.AccumulateNumber == 0)
        RenderTarget[DispatchRaysIndex().xy].xyz = TotalRadiance;
    else
        RenderTarget[DispatchRaysIndex().xy].xyz = lerp(RenderTarget[DispatchRaysIndex().xy].xyz, TotalRadiance, 1.0f / g_sceneCB.AccumulateNumber);
}

[shader("closesthit")]
void MeshClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    // SRV_UAV DescHeap
    StructuredBuffer<uint2> VertexIndexOffset;
    StructuredBuffer<uint> IndexBindless;
    StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam;
    ByteAddressBuffer VerticesBindless;
    ConstantBuffer<MaterialUniformBuffer> MaterialBindless;

    uint InstanceIdx = InstanceIndex();
    GetShaderBindingResources( 
        VertexIndexOffset,
        IndexBindless,
        RenderObjParam,
        VerticesBindless,
        MaterialBindless,
        InstanceIdx);

    uint VertexOffset = VertexIndexOffset[0].x;
    uint IndexOffset = VertexIndexOffset[0].y;

    uint PrimIdx = PrimitiveIndex();
    uint3 Indices = uint3(
        IndexBindless[IndexOffset + PrimIdx * 3],
        IndexBindless[IndexOffset + PrimIdx * 3 + 1],
        IndexBindless[IndexOffset + PrimIdx * 3 + 2]);

    jVertex Vertex0 = GetVertex(VerticesBindless, Indices.x + VertexOffset);
    jVertex Vertex1 = GetVertex(VerticesBindless, Indices.y + VertexOffset);
    jVertex Vertex2 = GetVertex(VerticesBindless, Indices.z + VertexOffset);

    // Make triangle normal
    float3 FaceNormal = HitAttribute(Vertex0.Normal
        , Vertex1.Normal
        , Vertex2.Normal, attr);
    FaceNormal = normalize(FaceNormal);
    
    float3 WorldNormal = 0;

    const bool IsUsingWorldNormal = true;
    if (IsUsingWorldNormal)
    {
        WorldNormal = FaceNormal;
    }
    else
    {
        WorldNormal = mul((float3x3)RenderObjParam[0].M, FaceNormal);
    }

    // Set hit point to payload
    payload.HitPos = HitWorldPosition();

    if (any(MaterialBindless.emission))
    {
        payload.Radiance += MaterialBindless.emission;
    }

    float3 SampleDir;
    float SamplePDF;
    float3 BRDF_Cos;
    SamplingBRDF(SampleDir, SamplePDF, BRDF_Cos, WorldNormal, WorldRayDirection(), MaterialBindless, payload.seed);

    if(dot(SampleDir, WorldNormal) <= 0)
    {
        payload.Radiance = float3(0, 1, 0);
		payload.RecursionDepth = MAX_RECURSION_DEPTH;
    }

    payload.Attenuation = BRDF_Cos / SamplePDF;
    payload.HitReflectDir = SampleDir;
}

[shader("anyhit")]
void MeshAnyHitShader(inout RayPayload payload, in MyAttributes attr)
{
}

[shader("miss")]
void MeshMissShader(inout RayPayload payload)
{
    payload.RecursionDepth = MAX_RECURSION_DEPTH;
}

//////////////////////////////////////////////////////////////////////////

[shader("closesthit")]
void LightClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    // SRV_UAV DescHeap
    StructuredBuffer<uint2> VertexIndexOffset;
    StructuredBuffer<uint> IndexBindless;
    StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam;
    ByteAddressBuffer VerticesBindless;
    ConstantBuffer<MaterialUniformBuffer> MaterialBindless;

    uint InstanceIdx = InstanceIndex();
    GetShaderBindingResources( 
        VertexIndexOffset,
        IndexBindless,
        RenderObjParam,
        VerticesBindless,
        MaterialBindless,
        InstanceIdx);

    if (MaterialBindless.lightId == -1)
    {
        return;
    }

    ConstantBuffer<LightUniformBuffer> LightBuffer;
    GetLightShaderBindingResource(LightBuffer, MaterialBindless.lightId);

    // Set hit point to payload
    payload.HitPos = HitWorldPosition();

    if (any(LightBuffer.emission))
    {
        payload.Radiance += LightBuffer.emission;
    }
    payload.RecursionDepth = MAX_RECURSION_DEPTH;
}

[shader("anyhit")]
void LightAnyHitShader(inout RayPayload payload, in MyAttributes attr)
{
}

[shader("miss")]
void LightMissShader(inout RayPayload payload)
{
}
