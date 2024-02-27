#include "common.hlsl"
#include "PBR.hlsl"

#ifndef USE_HLSL_DYNAMIC_RESOURCE
#define USE_HLSL_DYNAMIC_RESOURCE 0
#endif

#ifndef USE_BINDLESS_RESOURCE
#define USE_BINDLESS_RESOURCE 0
#endif

#define MAX_RECURSION_DEPTH 4

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float3 cameraPosition;
    float focalDistance;
    float3 lightPosition;
    float lensRadius;
    float3 lightAmbientColor;
    uint NumOfStartingRay;
    float3 lightDiffuseColor;
    float Padding0;                 // for 16 byte align
    float3 cameraDirection;
    float Padding1;                 // for 16 byte align
};

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u1, space0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b2, space0);
SamplerState AlbedoTextureSampler : register(s3, space0);
SamplerState PBRSamplerState : register(s4, space0);

#if USE_BINDLESS_RESOURCE
TextureCube<float4> IrradianceMapArray[] : register(t0, space1);
TextureCube<float4> PrefilteredEnvMapArray[] : register(t0, space2);
StructuredBuffer<uint2> VertexIndexOffsetArray[] : register(t0, space3);
StructuredBuffer<uint> IndexBindlessArray[] : register(t0, space4);
StructuredBuffer<RenderObjectUniformBuffer> RenderObjParamArray[] : register(t0, space5);
ByteAddressBuffer VerticesBindlessArray[] : register(t0, space6);
Texture2D AlbedoTextureArray[] : register(t0, space7);
Texture2D NormalTextureArray[] : register(t0, space8);
Texture2D RMTextureArray[] : register(t0, space9);
#endif // USE_BINDLESS_RESOURCE

jVertex GetVertex(in ByteAddressBuffer buffer, uint index)
{
    uint address = index * VERTEX_STRID;
    
    jVertex r;
    r.Pos = asfloat(buffer.Load3(address));
    r.Normal = asfloat(buffer.Load3(address + 12));
    r.Tangent = asfloat(buffer.Load3(address + 24));
    r.Bitangent = asfloat(buffer.Load3(address + 36));
    r.TexCoord = asfloat(buffer.Load2(address + 48));
    return r;
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
    uint currentRecursionDepth;
};

float3 Reflect(float3 v, float3 n)
{
    return v - 2 * dot(v, n) * n;
}

float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

float2 HitAttribute(float2 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

float3 HitAttribute(float3 A, float3 B, float3 C, BuiltInTriangleIntersectionAttributes attr)
{
    return A +
        attr.barycentrics.x * (B - A) +
        attr.barycentrics.y * (C - A);
}

float2 HitAttribute(float2 A, float2 B, float2 C, BuiltInTriangleIntersectionAttributes attr)
{
    return A +
        attr.barycentrics.x * (B - A) +
        attr.barycentrics.y * (C - A);
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline float3 GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(g_sceneCB.projectionToWorld, float4(screenPos, 0, 1));

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);

    return world.xyz;
}

void GetShaderBindingResources(
    inout TextureCube<float4> IrradianceMap,
    inout TextureCube<float4> PrefilteredEnvMap,
    inout StructuredBuffer<uint2> VertexIndexOffset,
    inout StructuredBuffer<uint> IndexBindless,
    inout StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam,
    inout ByteAddressBuffer VerticesBindless,
    inout Texture2D AlbedoTexture,
    inout Texture2D NormalTexture,
    inout Texture2D RMTexture,
    in uint InstanceIdx)
{
    uint PerPrimOffset = 3;
    uint Stride = 7;

#if USE_HLSL_DYNAMIC_RESOURCE
    IrradianceMap = ResourceDescriptorHeap[1];
    PrefilteredEnvMap = ResourceDescriptorHeap[2];

    VertexIndexOffset = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    IndexBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    RenderObjParam = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    VerticesBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    AlbedoTexture = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    NormalTexture = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    RMTexture = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
#elif USE_BINDLESS_RESOURCE
    IrradianceMap = IrradianceMapArray[0];
    PrefilteredEnvMap = PrefilteredEnvMapArray[0];

    VertexIndexOffset = VertexIndexOffsetArray[InstanceIdx];
    IndexBindless = IndexBindlessArray[InstanceIdx];
    RenderObjParam = RenderObjParamArray[InstanceIdx];
    VerticesBindless = VerticesBindlessArray[InstanceIdx];
    AlbedoTexture = AlbedoTextureArray[InstanceIdx];
    NormalTexture = NormalTextureArray[InstanceIdx];
    RMTexture = RMTextureArray[InstanceIdx];
#endif // USE_HLSL_DYNAMIC_RESOURCE
}

float3 random_in_unit_sphere2(int i)
{
    float2 uv = DispatchRaysIndex().xy + float2(i, i * 2.0f);
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    float noiseZ = (frac(sin(dot(uv, float2(12.9898, 78.233) * 3.0)) * 43758.5453));

    float3 randomUniSphere = float3(noiseX, noiseY, noiseZ) * 2.0f - 1.0f;
    if (length(randomUniSphere) <= 1.0f)
        return randomUniSphere;

    return normalize(randomUniSphere);
}

float3 random_in_unit_sphere()
{
    float2 uv = DispatchRaysIndex().xy + float2(RayTCurrent(), RayTCurrent() * 2.0f);
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * RayTCurrent() * 2.0f)) * 43758.5453));
    float noiseZ = (frac(sin(dot(uv, float2(12.9898, 78.233) * RayTCurrent())) * 43758.5453));

    float3 randomUniSphere = float3(noiseX, noiseY, noiseZ) * 2.0f - 0.5f;
    if (length(randomUniSphere) <= 1.0f)
        return randomUniSphere;

    return normalize(randomUniSphere);
}

float3 random_in_hemisphere(float3 normal)
{
    float3 in_unit_sphere = random_in_unit_sphere();
    if (dot(in_unit_sphere, normal) > 0.0)  // Check it random vector is same side of the hemisphere which centered normal vector
        return in_unit_sphere;

    return -in_unit_sphere;
}

inline void ApplyDepthOfField(inout float3 origin, inout float3 direction, int randomIndex)
{
    float ft = g_sceneCB.focalDistance / length(direction);     // Find ft that can intersect with the focal plane
    
    // Make a uv based on world-space camera direction
    float3 u = normalize(cross(g_sceneCB.cameraDirection.xyz, float3(0, 1, 0)));
    float3 v = normalize(cross(g_sceneCB.cameraDirection.xyz, u));
    
    // Move this random of uv direction(Max lensRadius)
    float2 lensRandom = random_in_unit_sphere2(randomIndex).xy * g_sceneCB.lensRadius;
    float3 offsetOnLens = lensRandom.x * u + lensRandom.y * v;
    offsetOnLens = random_in_unit_sphere2(randomIndex).xyz * g_sceneCB.lensRadius;
    
    float3 pFocus = origin + ft * direction;

    origin.xyz += offsetOnLens; // Set the origin as a random location in lens
    direction = normalize(pFocus - origin);
}

// Diffuse lighting calculation
float4 CalculationDiffuseLighting(float3 hitPosition, float3 normal)
{
    float3 pixelToLight = normalize(g_sceneCB.lightPosition.xyz - hitPosition);

    // Diffuse �⿩
    float fNDotL = max(0.0f, dot(pixelToLight, normal));
    //return g_localRootSigCB.albedo * g_sceneCB.lightDiffuseColor * fNDotL;
    return float4((float3(1, 1, 1) * g_sceneCB.lightDiffuseColor * fNDotL), 1.0f);
}

float Schlick(float cosine, float ref_idx)
{
    // Use Schlick's approximation for reflectance.
    float r0 = (1 - ref_idx) / (1 + ref_idx);
    r0 = r0 * r0;
    return r0 + (1 - r0) * pow((1 - cosine), 5);
}

// 1. Default Reflection
float3 MakeDefaultReflection(float3 normal)
{
    return normal + random_in_unit_sphere();
}

// 2. Lambertian reflection
float3 MakeLambertianReflection(float3 normal)
{
    return random_in_hemisphere(normal);
}

// 3. Mirror Reflection
float3 MakeMirrorReflection(float3 normal, float fuzzFactor = 0.0f)
{
    float3 worldNormal = normalize(mul((float3x3)ObjectToWorld3x4(), normal));
    float3 reflectedDir = Reflect(normalize(WorldRayDirection()), worldNormal);

    if (fuzzFactor > 0)
    {
        reflectedDir += random_in_unit_sphere() * fuzzFactor;
    }

    return reflectedDir;
}

float3 MakeRefract(float3 uv, float3 normal, float etai_over_etat)
{
    float cos_theta = min(dot(-uv, normal), 1.0);
    float3 r_out_perp = etai_over_etat * (uv + cos_theta * normal);
    float r_out_perp_length = length(r_out_perp);
    float3 r_out_parallel = -sqrt(abs(1.0 - r_out_perp_length * r_out_perp_length)) * normal;
    return r_out_perp + r_out_parallel;
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;

    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    int samples = g_sceneCB.NumOfStartingRay;

    for (int i = 0; i < samples; ++i)
    {
        GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);
        ApplyDepthOfField(origin, rayDir, i * 2.0);

        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = rayDir + float3(random_in_unit_sphere2(i).xy / DispatchRaysDimensions().xy, 0.0f);

        // Avoid aliasing issues by setting TMin to a small nonzero value. - floating point error
        // Keep TMin small to prevent geometry missing in contact areas
        ray.TMin = 0.001;
        ray.TMax = 10000.0;

        // RayPayload payload = { float4(0, 0, 0, 0) };
        RayPayload payload = { float4(1.0f, 1.0f, 1.0f, 1.0f), 1 };
        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);

        color += payload.color;
    }

    color /= samples;

    // Record ray tracked colors in the output texture
    RenderTarget[DispatchRaysIndex().xy] = color;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    uint InstanceIdx = InstanceIndex();

    // SRV_UAV DescHeap
    TextureCube<float4> IrradianceMap;
    TextureCube<float4> PrefilteredEnvMap;
    StructuredBuffer<uint2> VertexIndexOffset;
    StructuredBuffer<uint> IndexBindless;
    StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam;
    ByteAddressBuffer VerticesBindless;
    Texture2D AlbedoTexture;
    Texture2D NormalTexture;
    Texture2D RMTexture;

    GetShaderBindingResources(IrradianceMap, 
        PrefilteredEnvMap, 
        VertexIndexOffset,
        IndexBindless,
        RenderObjParam,
        VerticesBindless,
        AlbedoTexture,
        NormalTexture,
        RMTexture,
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

    float3 triangleNormal = HitAttribute(Vertex0.Normal
        , Vertex1.Normal
        , Vertex2.Normal, attr);

    if (payload.currentRecursionDepth < MAX_RECURSION_DEPTH)
    {
        RayPayload newPayload;        
        newPayload.currentRecursionDepth = payload.currentRecursionDepth + 1;

        // Tracing ray
        RayDesc ray;
        ray.Origin = hitPosition;
        bool IsPlane = InstanceID() == 0;       // this is kind of trick for easy implementation. I placed plane instance at 0.
        if (IsPlane)
        {
            newPayload.color = payload.color * float4(0.5f, 0.5f, 0.5f, 1.0f);
            ray.Direction = MakeLambertianReflection(triangleNormal);
        }
        else
        {
            uint selectedIndex = (InstanceID() - 1) % 5;
            switch (selectedIndex)
            {
            // Refraction
            case 0:
            {
                //newPayload.color = payload.color * g_localRootSigCB.albedo;
                newPayload.color = payload.color * float4(1, 1, 1, 1);

                // 5. Refraction corrected
                float cos_theta = min(dot(-WorldRayDirection(), triangleNormal), 1.0);
                float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

                float ir = 1.5f;
                bool IsFrontFace = true;
                float refraction_ratio = IsFrontFace ? (1.0 / ir) : ir;
                bool cannot_refract = refraction_ratio * sin_theta > 1.0;

                if (cannot_refract || Schlick(cos_theta, refraction_ratio) > (random_in_unit_sphere().x * 0.5f + 1.0f))
                    ray.Direction = MakeMirrorReflection(triangleNormal);
                else
                    ray.Direction = MakeRefract(WorldRayDirection(), triangleNormal, refraction_ratio);
            }
            break;

            // Mirror Reflection
            case 1:
                ray.Direction = MakeMirrorReflection(triangleNormal, 0.0f);
                newPayload.color = payload.color * float4(1, 1, 1, 1);
                break;

            // Lambertian Reflection            
            case 2:
                newPayload.color = payload.color * float4(0.8f, 0.2f, 0.2f, 1.0f);
                ray.Direction = MakeLambertianReflection(triangleNormal);
                break;
            case 3:
                newPayload.color = payload.color * float4(0.2f, 0.8f, 0.2f, 1.0f);
                ray.Direction = MakeLambertianReflection(triangleNormal);
                break;
            case 4:
                newPayload.color = payload.color * float4(0.2f, 0.2f, 0.8f, 1.0f);
                ray.Direction = MakeLambertianReflection(triangleNormal);
                break;
            }
        }

        // Avoid aliasing issues by setting TMin to a small nonzero value. - floating point error
        // Keep TMin small to prevent geometry missing in contact areas
        ray.TMin = 0.01;
        ray.TMax = 10000.0;

        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, newPayload);

        payload.color = newPayload.color;
    }
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
     // Make a 't' value that is the factor scaled by using ray hit on background of Y axis.
    float2 xy = HitWorldPosition().xy;
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0f - 1.0f;;
    float4 world = mul(g_sceneCB.projectionToWorld, float4(screenPos, 0, 1));
    world.xyz /= world.w;
    float3 origin = g_sceneCB.cameraPosition.xyz;
    float3 direction = normalize(world.xyz - origin);

    float t = (direction.y + 1.0f) * 0.5f;
    float3 background = (1.0f - t) * float3(1.0f, 1.0f, 1.0f) + t * float3(0.5f, 0.7f, 1.0f);

    payload.color *= float4(background, 1.0f);
}
