#include "common.hlsl"
#include "BRDF.hlsl"

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
    int TransmittingInstanceIndex;
};

float DielectricFresnel(float cosThetaI, float eta)
{
    float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

    // Total internal reflection
    if (sinThetaTSq > 1.0)
        return 1.0;

    float cosThetaT = sqrt(max(1.0 - sinThetaTSq, 0.0));

    float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

    return 0.5f * (rs * rs + rp * rp);
}

bool Refract(in float3 wi, in float3 n, float eta, out float3 wt) 
{
    float cosThetaI = dot(n, wi);
    float sin2ThetaI = max(0.0f, 1.0f - cosThetaI * cosThetaI);
    float sin2ThetaT = eta * eta * sin2ThetaI;
    if (sin2ThetaT >= 1) return false;

    float cosThetaT = sqrt(1 - sin2ThetaT);

    wt = eta * -wi + (eta * cosThetaI - cosThetaT) * n;
    return true;
}

float3 SampleGGXVNDF(float3 V, float ax, float ay, float r1, float r2)
{
    float3 Vh = normalize(float3(ax * V.x, ay * V.y, V.z));

    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * (1.0f / sqrt(lensq)) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(r1);
    float phi = 2.0 * PI * r2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    return normalize(float3(ax * Nh.x, ay * Nh.y, max(0.0, Nh.z)));
}

void SamplingBRDF(out float3 SampleDir, out float SamplePDF, out float3 BRDF_Cos
    , in float3 WorldNormal, in float3 SurfaceToView, in ConstantBuffer<MaterialUniformBuffer> mat, inout RayPayload payload)
{
    float diffusePart = (1.0f - mat.metallic) * (1.0f - mat.specTrans);
    float specularPart = mat.metallic * (1.0f - mat.specTrans);
    float glassPart = (1.0 - mat.metallic) * mat.specTrans;

    SamplePDF = 0;
    BRDF_Cos = 0;

// In, out intersection check within any hit for glass sphere
#define TRANSMITTANCE_TEST 0
#if TRANSMITTANCE_TEST
    if (payload.TransmittingInstanceIndex != -1)
    {
        payload.Radiance = float3(10, 0, 0);
        payload.Attenuation = 1;
        return;
    }
#endif // TRANSMITTANCE_TEST

    //payload.TransmittingInstanceIndex = -1;

    if (diffusePart > 0.0f)
    {
        // Lambertian surface
        SampleDir = CosWeightedSampleHemisphere(payload.seed);
        float cosine_theta = SampleDir.z;
        SampleDir = ToWorld(WorldNormal, SampleDir);
    

        float3 BRDF = INV_PI * mat.baseColor;
        BRDF_Cos += BRDF * cosine_theta * diffusePart;
        SamplePDF += (INV_PI * cosine_theta) * diffusePart;
    }

    if (mat.metallic > 0.0f)
    {
        // Cook torrance brdf from https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
        float3 F0 = mat.baseColor;

        float r1 = Random_0_1(payload.seed);
        float r2 = Random_0_1(payload.seed);
        float3 WorldHalf = normalize(ImportanceSampleGGX(float2(r1, r2), mat.roughness, WorldNormal));

        //SampleDir = 2.0f * dot(SurfaceToView, WorldHalf) * WorldHalf - SurfaceToView;
        SampleDir = reflect(-SurfaceToView, WorldHalf);

        float NoV = saturate(dot(WorldNormal, SurfaceToView));
        float NoL = saturate(dot(WorldNormal, SampleDir));
        float NoH = saturate(dot(WorldNormal, WorldHalf));
        float VoH = saturate(dot(SurfaceToView, WorldHalf));

        float cosine_theta = NoL;
        if (cosine_theta < 0)
        {
            // Underneath skip.
            BRDF_Cos = 0;
            SamplePDF = 0;
        }
        else
        {
            float D = DistributionGGX(WorldNormal, WorldHalf, mat.roughness);
            float G = GeometrySmith(mat.roughness, NoV, NoL);
            float3 F = FresnelSchlick(cosine_theta, F0);
            float3 BRDF = F * (D * G / (4 * NoL * NoV));
            
            SamplePDF += (D * NoH / (4 * VoH)) * mat.metallic;
            BRDF_Cos += (BRDF * cosine_theta) * mat.metallic;
        }
    }

    if (glassPart > 0.0f)
    {
        // Cook torrance brdf from https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
        float3 F0 = mat.baseColor;

        float r1 = Random_0_1(payload.seed);
        float r2 = Random_0_1(payload.seed);
        float3 WorldHalf = normalize(ImportanceSampleGGX(float2(r1, r2), mat.roughness, WorldNormal));

        bool IntoMedium = dot(SurfaceToView, WorldHalf) > 0.0f;
        float ior = mat.ior;
        float eta = IntoMedium ? (1.0 / ior) : ior;
        float F = DielectricFresnel(abs(dot(SurfaceToView, WorldHalf)), eta);

        if (Random_0_1(payload.seed) < F)
        {
            SampleDir = reflect(-SurfaceToView, WorldHalf);
            float cosine_theta = dot(SampleDir, WorldHalf);

            BRDF_Cos = F;
            SamplePDF = F;
        }
        else
        {
            SampleDir = refract(-SurfaceToView, WorldHalf, eta);
            float cosine_theta = dot(SampleDir, WorldNormal);
            
            BRDF_Cos = 1.0-F;
            SamplePDF = 1.0-F;
            payload.TransmittingInstanceIndex = InstanceIndex();
        }
    }

}

[shader("raygeneration")]
void RaygenShader()
{
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, g_sceneCB.AccumulateNumber);

    // todo : set from shader
    #define MAX_RECURSION_DEPTH 10
    #define MAX_PIXEL_PER_RAY 10
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
        payload.TransmittingInstanceIndex = -1;

        float3 radiance = 0.0f;
        float3 attenuation = 1.0f;

        while (payload.RecursionDepth < MAX_RECURSION_DEPTH)
        {
            TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);
            //TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
            /*
            if (payload.TransmittingInstanceIndex == -1)
                TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);
            else
                TraceRay(Scene, RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);
            */
            radiance += attenuation * payload.Radiance;
            attenuation = max(0, attenuation * payload.Attenuation);

            ray.Origin = payload.HitPos;
            ray.Direction = payload.HitReflectDir;
            ++payload.RecursionDepth;
        }

        TotalRadiance += radiance;
        
        seed = payload.seed;
    }
    TotalRadiance /= MAX_PIXEL_PER_RAY;

    if (g_sceneCB.AccumulateNumber == 0)
    {
        RenderTarget[DispatchRaysIndex().xy].xyz = TotalRadiance;
    }
    else
    {
        RenderTarget[DispatchRaysIndex().xy].xyz = lerp(RenderTarget[DispatchRaysIndex().xy].xyz, TotalRadiance, 1.0f / g_sceneCB.AccumulateNumber);
    }

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

    if (dot(WorldNormal, -WorldRayDirection()) <= 0.0f)
    {
        payload.Attenuation = 1;
        payload.Radiance = 0;
        payload.RecursionDepth = MAX_RECURSION_DEPTH;
        return;
    }

    if (any(MaterialBindless.emission))
    {
        payload.Radiance += MaterialBindless.emission;
    }

    float3 SampleDir;
    float SamplePDF;
    float3 BRDF_Cos;
    SamplingBRDF(SampleDir, SamplePDF, BRDF_Cos, WorldNormal, -WorldRayDirection(), MaterialBindless, payload);

    //if(dot(SampleDir, WorldNormal) <= 0)
    {
        //payload.Attenuation = 1;
        //payload.Radiance = 0;
		//payload.RecursionDepth = MAX_RECURSION_DEPTH;
        //return;
    }

    payload.Attenuation = BRDF_Cos / SamplePDF;
    payload.HitReflectDir = SampleDir;
}

[shader("anyhit")]
void MeshAnyHitShader(inout RayPayload payload, in MyAttributes attr)
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
/*
    if (MaterialBindless.specTrans > 0)
    {
        if (payload.TransmittingInstanceIndex == InstanceIndex())
        {
            
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

            bool IsOutMedium = dot(WorldRayDirection(), WorldNormal) >= 0.0;
            float eta =  !IsOutMedium ? (1.0 / MaterialBindless.ior) : MaterialBindless.ior;
            payload.HitPos = HitWorldPosition();
            //eta = 1.0f;
            //payload.HitReflectDir = refract(WorldRayDirection(), WorldNormal, 1.0 / eta);
            
            float F = DielectricFresnel(abs(dot(-WorldRayDirection(), WorldNormal)), eta);

            if (Random_0_1(payload.seed) < F)
            {
                payload.HitReflectDir = reflect(WorldRayDirection(), WorldNormal);
            }
            else
            {
                payload.HitReflectDir = refract(WorldRayDirection(), WorldNormal, eta);
                payload.TransmittingInstanceIndex = -1;
            }
            
            
            IgnoreHit();
            return;
        }
    }
*/
/*
    if (payload.TransmittingInstanceIndex != -1)
    {
        if (0 < payload.TransmittingInstanceIndex)
        {
            IgnoreHit();
            return;
        }
    }
*/
    AcceptHitAndEndSearch();
}

[shader("miss")]
void MeshMissShader(inout RayPayload payload)
{
    //payload.Radiance = float3(1, 0, 0);
    payload.Radiance = 0;
    payload.Attenuation = 1;
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
