#include "common.hlsl"
#include "PBR.hlsl"

#ifndef USE_HLSL_DYNAMIC_RESOURCE
#define USE_HLSL_DYNAMIC_RESOURCE 0
#endif

#ifndef USE_BINDLESS_RESOURCE
#define USE_BINDLESS_RESOURCE 0
#endif

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float4 ViewRect;
    float3 cameraPosition;
    float AORadius;
    int Clear;
    uint RayPerPixel;
    float2 HaltonJitter;
    int FrameNumber;
    float3 Padding0;        // for 16 byte align
};

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u1, space0);
Texture2D DepthTexture : register(t2, space0);
Texture2D GBuffer0_Normal : register(t3, space0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b4, space0);
SamplerState AlbedoTextureSampler : register(s5, space0);
SamplerState PBRSamplerState : register(s6, space0);

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

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
};

float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
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

float3 random_in_unit_sphere(inout uint seed)
{
    //float rand = RayTCurrent();
    float r = Random_0_1(seed) + 0.01f;        // To avoid 0 value for random variable
    float2 uv = DispatchRaysIndex().xy + float2(r, r * 2.0f);
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * r * 2.0f)) * 43758.5453));
    float noiseZ = (frac(sin(dot(uv, float2(12.9898, 78.233) * r)) * 43758.5453));

    float3 randomUniSphere = float3(noiseX, noiseY, noiseZ) * 2.0f - 1.0f;
    if (length(randomUniSphere) <= 1.0f)
        return randomUniSphere;

    return normalize(randomUniSphere);
}

float3 random_in_hemisphere(float3 normal, inout uint seed)
{
    float3 in_unit_sphere = random_in_unit_sphere(seed);
    if (dot(in_unit_sphere, normal) > 0.0)  // 노멀 기준으로 같은 반구 방향인지?
        return in_unit_sphere;

    return -in_unit_sphere;
}

float3 random_in_unit_sphere(float3 seed)
{
    float r = Random_0_1(seed.x) + 0.01f; // To avoid 0 value for random variable
    float g = Random_0_1(seed.y) + 0.01f; // To avoid 0 value for random variable
    float b = Random_0_1(seed.z) + 0.01f; // To avoid 0 value for random variable
    float2 uv = float2(DispatchRaysIndex().x * 2 + r, DispatchRaysIndex().y + r * 2.0f);
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * g * 2.0f)) * 43758.5453));
    float noiseZ = (frac(sin(dot(uv, float2(12.9898, 78.233) * b)) * 43758.5453));

    float3 randomUniSphere = float3(noiseX, noiseY, noiseZ) * 2.0f - 1.0f;
    if (length(randomUniSphere) <= 1.0f)
        return randomUniSphere;

    return normalize(randomUniSphere);
}

float3 random_in_hemisphere(float3 normal, float3 seed)
{
    float3 in_unit_sphere = random_in_unit_sphere(seed);
    if (dot(in_unit_sphere, normal) > 0.0)  // 노멀 기준으로 같은 반구 방향인지?
        return in_unit_sphere;

    return -in_unit_sphere;
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

float3 RayPlaneIntersection(float3 planeOrigin, float3 planeNormal, float3 rayOrigin, float3 rayDirection)
{
    float t = dot(-planeNormal, rayOrigin - planeOrigin) / dot(planeNormal, rayDirection);
    return rayOrigin + rayDirection * t;
}

/*
    REF: https://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates
    From "Real-Time Collision Detection" by Christer Ericson
*/
float3 BarycentricCoordinates(float3 pt, float3 v0, float3 v1, float3 v2)
{
    float3 e0 = v1 - v0;
    float3 e1 = v2 - v0;
    float3 e2 = pt - v0;
    float d00 = dot(e0, e0);
    float d01 = dot(e0, e1);
    float d11 = dot(e1, e1);
    float d20 = dot(e2, e0);
    float d21 = dot(e2, e1);
    float denom = 1.0 / (d00 * d11 - d01 * d01);
    float v = (d11 * d20 - d01 * d21) * denom;
    float w = (d00 * d21 - d01 * d20) * denom;    float u = 1.0 - v - w;
    return float3(u, v, w);
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float2 UV = DispatchRaysIndex().xy / g_sceneCB.ViewRect.zw;
    UV += g_sceneCB.HaltonJitter.xy; // Apply Jittering from Halton Sequence

    float3 WorldPos = CalcWorldPositionFromDepth(DepthTexture, AlbedoTextureSampler, UV, g_sceneCB.projectionToWorld);
    //float2 OctNormal = GBuffer0_Normal.SampleLevel(AlbedoTextureSampler, UV, 0);
    //float3 WorldNormal = normalize(DecodeOctNormal(OctNormal));
    float3 WorldNormal = GBuffer0_Normal.SampleLevel(AlbedoTextureSampler, UV, 0).xyz * 2 - 1;

    float3 FinalAO = 0;
    float AccumulateCount = 0.0f;
    if (!g_sceneCB.Clear)
    {
        FinalAO = RenderTarget[DispatchRaysIndex().xy].xyz;
        AccumulateCount = RenderTarget[DispatchRaysIndex().xy].w;
    }

    if (AccumulateCount > 500)
        return;

    uint seed = InitRandomSeed(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, g_sceneCB.FrameNumber);
    
    for(int i=0;i<g_sceneCB.RayPerPixel;++i)
    {
        RayDesc ray;
        ray.Origin = WorldPos;
        ray.Direction = random_in_hemisphere(WorldNormal, seed);
        ray.TMin = 0.001; // Small epsilon to avoid self intersection.
        ray.TMax = g_sceneCB.AORadius;
    
        RayPayload payload = { float4(1.0f, 1.0f, 1.0f, 1.0f) };
        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);

        ++AccumulateCount;

        // Incremental Average : https://blog.demofox.org/2016/08/23/incremental-averaging/
        FinalAO = lerp(FinalAO.xyz, payload.color.xyz, 1.0 / AccumulateCount);
    }
    RenderTarget[DispatchRaysIndex().xy] = float4(FinalAO, AccumulateCount);
}

[shader("anyhit")]
void MyAnyHitShader(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = float4(0, 0, 0, 1);
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = float4(0, 0, 0, 1);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.color = float4(1, 1, 1, 1);
}