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

    int baseColorTexId;
    int metallicRoughnessTexID;
    int normalmapTexID;
    int emissionmapTexID;

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
SamplerState DefaultSamplerState : register(s3, space0);

StructuredBuffer<uint2> VertexIndexOffsetArray[] : register(t0, space1);
StructuredBuffer<uint> IndexBindlessArray[] : register(t0, space2);
StructuredBuffer<RenderObjectUniformBuffer> RenderObjParamArray[] : register(t0, space3);
ByteAddressBuffer VerticesBindlessArray[] : register(t0, space4);
ConstantBuffer<MaterialUniformBuffer> MaterialBindlessArray[] : register(b0, space5);
ConstantBuffer<LightUniformBuffer> LightBindlessArray[] : register(b0, space6);
Texture2D TextureBindlessArray[] : register(t0, space7);

MaterialUniformBuffer GetMaterial(inout float3 InOutWorldNormal, in MaterialUniformBuffer InMaterial, float2 InUV)
{
    MaterialUniformBuffer mat;
    mat = InMaterial;

    // Base Color Map
    if (mat.baseColorTexId >= 0)
    {
        float4 col = TextureBindlessArray[mat.baseColorTexId].SampleLevel(DefaultSamplerState, InUV, 0);
        mat.baseColor.rgb *= pow(col.rgb, 2.2);
        mat.opacity *= col.a;
    }

    // Metallic Roughness Map
    if (mat.metallicRoughnessTexID >= 0)
    {
        float4 matRgh = TextureBindlessArray[mat.metallicRoughnessTexID].SampleLevel(DefaultSamplerState, InUV, 0);
        mat.metallic = matRgh.x;
        mat.roughness = max(matRgh.y * matRgh.y, 0.001);
    }

    // Normal Map
    if (mat.normalmapTexID >= 0)
    {
        float3 texNormal = TextureBindlessArray[mat.normalmapTexID].SampleLevel(DefaultSamplerState, InUV, 0).xyz;
        texNormal.y = 1.0 - texNormal.y;
        texNormal = normalize(texNormal * 2.0 - 1.0);
        

        float3 origNormal = InOutWorldNormal;
        float3 T, B;
        MakeTB_From_N(InOutWorldNormal, T, B);
        InOutWorldNormal = normalize(T * texNormal.x + B * texNormal.y + origNormal * texNormal.z);
        //state.ffnormal = dot(origNormal, r.direction) <= 0.0 ? origNormal : -state.normal;
    }

    // Emission Map
    if (mat.emissionmapTexID >= 0)
        mat.emission = pow(TextureBindlessArray[mat.emissionmapTexID].SampleLevel(DefaultSamplerState, InUV, 0).xyz, 2.2);

    return mat;
}

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
    inout MaterialUniformBuffer MaterialBuffer,
    in uint InstanceIdx)
{
    VertexIndexOffsetBuffer = VertexIndexOffsetArray[InstanceIdx];
    IndexBuffer = IndexBindlessArray[InstanceIdx];
    RenderObjParam = RenderObjParamArray[InstanceIdx];
    VerticesBuffer = VerticesBindlessArray[InstanceIdx];
    MaterialBuffer = MaterialBindlessArray[InstanceIdx];
}

void GetLightShaderBindingResource(inout LightUniformBuffer LightBuffer, int LightId)
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
    int PenetratingInstanceIndex;

    bool IsRayPenetratingInstance() { return PenetratingInstanceIndex != -1; }
    void SetPenetratingInstnaceIndex(uint InstanceIndex) { PenetratingInstanceIndex = InstanceIndex; }
    void ResetPenetratingInstanceIndex() { PenetratingInstanceIndex = -1; }
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

// https://github.com/knightcrawler25/GLSL-PathTracer/blob/master/src/shaders/common/sampling.glsl
float SchlickWeight(float u)
{
    float m = clamp(1.0 - u, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

void SamplingBRDF(out float3 SampleDir, out float SamplePDF, out float3 BRDF_Cos
    , in float3 WorldNormal, in float3 WorldFaceNormal, in float3 SurfaceToView, in MaterialUniformBuffer mat, inout RayPayload payload)
{
    SamplePDF = 0;
    BRDF_Cos = 0;
    
    float diffusePart = (1.0f - mat.metallic) * (1.0f - mat.specTrans);
    float specularPart = mat.metallic * (1.0f - mat.specTrans);
    float glassPart = (1.0 - mat.metallic) * mat.specTrans;
    float clearcoatPart = 0.25 * mat.clearcoat;

#define TRANSMITTANCE_TEST 0    // Debugging code : In, out intersection check within any hit for glass sphere
#if TRANSMITTANCE_TEST
    if (payload.IsRayPenetratingInstance() && glassPart <= 0)       // If intersection material is not glassPart, it's always not Penetrating.
    {
        payload.Radiance = float3(10, 0, 0);
        payload.Attenuation = 1;
        return;
    }
#endif // TRANSMITTANCE_TEST
    
    // If penetrating instance(glass type), it is always intersecting glass part.
    if (payload.IsRayPenetratingInstance())
    {
        diffusePart = 0.0;
        specularPart = 0.0;
        clearcoatPart = 0.0;
    }    
    
    //diffusePart = 1.0;  // test disable
    //specularPart = 0.0;  // test disable
    //clearcoatPart = 1.0;  // test disable
    //glassPart = 0;  // test disable

    float totalWeight = 1.0f / (diffusePart + specularPart + glassPart + clearcoatPart);
    float diffuseWeight = diffusePart * totalWeight;
    float specularWeight = specularPart * totalWeight;
    float clearcoatWeight = clearcoatPart * totalWeight;
    float glassWeight = glassPart * totalWeight;

    // CDF of the sampling probabilities
    float cdf[4];
    cdf[0] = diffuseWeight;
    cdf[1] = cdf[0] + specularWeight;
    cdf[2] = cdf[1] + clearcoatWeight;
    cdf[3] = cdf[2] + glassWeight;

    float3 WorldHalf = 0;
    float cosine_theta = 0;

    float r3 = Random_0_1(payload.seed);
    if (r3 < cdf[0]) // diffusePart
    {
        // Lambertian surface
        SampleDir = CosWeightedSampleHemisphere(payload.seed);
        cosine_theta = SampleDir.z;
        SampleDir = ToWorld(WorldNormal, SampleDir);
        
        if (diffusePart > 0.0f)
        {
            if (cosine_theta < 0)
            {
            // Underneath skip.
            }
            else
            {
                float3 BRDF = INV_PI * mat.baseColor;
                BRDF_Cos += BRDF * diffusePart;
                SamplePDF += (INV_PI * cosine_theta) * diffuseWeight;
            }
        }
    }
    else if (r3 < cdf[1]) // specularPart
    {
        // Cook torrance brdf from https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
        float r1 = Random_0_1(payload.seed);
        float r2 = Random_0_1(payload.seed);
        WorldHalf = normalize(ImportanceSampleGGX(float2(r1, r2), mat.roughness, WorldNormal));
        SampleDir = reflect(-SurfaceToView, WorldHalf);
        cosine_theta = saturate(dot(WorldNormal, SampleDir));
        
        if (specularPart > 0.0f)
        {
            float NoV = saturate(dot(WorldNormal, SurfaceToView));
            float NoL = saturate(dot(WorldNormal, SampleDir));
            float NoH = saturate(dot(WorldNormal, WorldHalf));
            float VoH = saturate(dot(SurfaceToView, WorldHalf));

            if (cosine_theta < 0)
            {
            // Underneath skip.
            }
            else
            {
                float3 F0 = mat.baseColor;

                float D = DistributionGGX(WorldNormal, WorldHalf, mat.roughness);
                float G = GeometrySmith(mat.roughness, NoV, NoL);
                float3 F = FresnelSchlick(cosine_theta, F0);
                float3 BRDF = F * (D * G / (4 * NoL * NoV));
            
                BRDF_Cos += BRDF * specularPart;
                SamplePDF += (D * NoH / (4 * VoH)) * specularWeight;
            }
        }
    }
    else if (r3 < cdf[2]) // clearcoatPart
    {
        // https://github.com/knightcrawler25/GLSL-PathTracer/blob/master/src/shaders/common/sampling.glsl
        float clearcoatRoughness = lerp(0.25, 0.1, mat.clearcoatGloss);

        float r1 = Random_0_1(payload.seed);
        float r2 = Random_0_1(payload.seed);
        WorldHalf = normalize(ImportanceSampleGGX(float2(r1, r2), clearcoatRoughness, WorldNormal));
        SampleDir = reflect(-SurfaceToView, WorldHalf);
        cosine_theta = saturate(dot(WorldNormal, SampleDir));
        
        if (clearcoatPart > 0.0f)
        {
            float NoV = saturate(dot(WorldNormal, SurfaceToView));
            float NoL = saturate(dot(WorldNormal, SampleDir));
            float NoH = saturate(dot(WorldNormal, WorldHalf));
            float VoH = saturate(dot(SurfaceToView, WorldHalf));
            if (cosine_theta < 0)
            {
                // Underneath skip.
            }
            else
            {
                float D = DistributionGGX(WorldNormal, WorldHalf, clearcoatRoughness);
                float G = GeometrySmith(0.25, NoV, NoL);
                float3 F = FresnelSchlick(cosine_theta, float3(1, 1, 1));
                float3 BRDF = F * (D * G / (4 * NoL * NoV));
            
                BRDF_Cos += BRDF * clearcoatPart;
                SamplePDF += (D * NoH / (4 * VoH)) * clearcoatWeight;
            }
        }
    }
    else if (r3 < cdf[3]) // glassPart
    {
        float NoV = dot(SurfaceToView, WorldNormal);
        float fNoV = dot(SurfaceToView, WorldFaceNormal);
        
        // Cook torrance brdf from https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf
        float3 F0 = mat.baseColor;

        float r1 = Random_0_1(payload.seed);
        float r2 = Random_0_1(payload.seed);
        float3 WorldHalf = normalize(ImportanceSampleGGX(float2(r1, r2), mat.roughness, WorldNormal));

        bool IntoMedium = NoV > 0.0f;
        float eta = IntoMedium ? (1.0 / mat.ior) : mat.ior;
            
        if (NoV <= 0)
            WorldHalf *= -1.0f;
            
        float F = DielectricFresnel(dot(SurfaceToView, WorldHalf), eta);

        if (Random_0_1(payload.seed) < F && NoV > 0)
        {
            SamplePDF = 1;
            BRDF_Cos = 1;
            SampleDir = reflect(-SurfaceToView, WorldHalf);
        }
        else
        {
            SamplePDF = 1;
            BRDF_Cos = 1;
            SampleDir = refract(-SurfaceToView, WorldHalf, eta);

            if (NoV > 0)
            {
                payload.SetPenetratingInstnaceIndex(InstanceIndex());
            }
            else
            {
                payload.ResetPenetratingInstanceIndex();
            }
        }
        cosine_theta = 1.0f;
    }

    BRDF_Cos *= cosine_theta;
}

// todo : set from shader
#define MAX_RECURSION_DEPTH 6
#define MAX_PIXEL_PER_RAY 5

[shader("raygeneration")]
void RaygenShader()
{
    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, g_sceneCB.AccumulateNumber);

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
        payload.ResetPenetratingInstanceIndex();

        float3 radiance = 0.0f;
        float3 attenuation = 1.0f;

        while (payload.RecursionDepth < MAX_RECURSION_DEPTH)
        {
#define FORCE_TWO_SIDE 0
#if FORCE_TWO_SIDE
            TraceRay(Scene, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
#else
            if (payload.IsRayPenetratingInstance())
                TraceRay(Scene, RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);
            else
                TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);
#endif // FORCE_TWO_SIDE
            
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
    MaterialUniformBuffer MaterialBindless;

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

    float3 FaceNormal = normalize(cross(Vertex2.Pos - Vertex0.Pos, Vertex1.Pos - Vertex0.Pos));
    
    // Make triangle normal
    float3 normal = HitAttribute(Vertex0.Normal
        , Vertex1.Normal
        , Vertex2.Normal, attr);
    normal = normalize(normal);
    
    float3 WorldNormal = 0;
    float3 WorldFaceNormal = 0;

    const bool IsUsingWorldNormal = true;
    if (IsUsingWorldNormal)
    {
        WorldNormal = normal;
    }
    else
    {
        WorldNormal = mul((float3x3) RenderObjParam[0].M, normal);
    }
    WorldFaceNormal = mul((float3x3) RenderObjParam[0].M, FaceNormal);

    float2 uv = HitAttribute(Vertex0.TexCoord, Vertex1.TexCoord, Vertex2.TexCoord, attr);
    MaterialUniformBuffer material = GetMaterial(WorldNormal, MaterialBindless, uv);
    
    // Set hit point to payload
    payload.HitPos = HitWorldPosition();
    
    float SamplePDF;
    float3 BRDF_Cos = 0;
    float3 SampleDir = 0;
    if (any(material.emission))
    {
        payload.Radiance += material.emission;
    }
    SamplingBRDF(SampleDir, SamplePDF, BRDF_Cos, WorldNormal, WorldFaceNormal, -WorldRayDirection(), material, payload);

    payload.Attenuation = BRDF_Cos / SamplePDF;
    payload.HitReflectDir = SampleDir;
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
    MaterialUniformBuffer MaterialBindless;

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

    LightUniformBuffer LightBuffer;
    GetLightShaderBindingResource(LightBuffer, MaterialBindless.lightId);

    // Set hit point to payload
    payload.HitPos = HitWorldPosition();

    if (any(LightBuffer.emission))
    {
        payload.Radiance += LightBuffer.emission;
    }
    payload.RecursionDepth = MAX_RECURSION_DEPTH;
}
