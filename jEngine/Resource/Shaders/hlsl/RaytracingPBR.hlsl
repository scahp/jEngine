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
    float focalDistance;
    float3 lightPosition;
    float lensRadius;
    float3 lightAmbientColor;
    uint NumOfStartingRay;
    float3 lightDiffuseColor;
    float Padding0;                 // for 16 byte align
    float3 cameraDirection;
    float Padding1;                 // for 16 byte align
    float3 lightDirection;
    float Padding2;                 // for 16 byte align
};

#define VERTEX_STRID 56
struct jVertex
{
	float3 Pos;
	float3 Normal;
	float3 Tangent;
	float3 Bitangent;
	float2 TexCoord;
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
};

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

float3 RayPlaneIntersection(float3 planeOrigin, float3 planeNormal, float3 rayOrigin, float3 rayDirection)
{
    float t = dot(-planeNormal, rayOrigin - planeOrigin) / dot(planeNormal, rayDirection);
    return rayOrigin + rayDirection * t;
}



// https://gist.github.com/keijiro/24f9d505fac238c9a2982c0d6911d8e3
// Hash function from H. Schechter & R. Bridson, goo.gl/RXiKaH
uint Hash(uint s)
{
    s ^= 2747636419u;
    s *= 2654435769u;
    s ^= s >> 16;
    s *= 2654435769u;
    s ^= s >> 16;
    s *= 2654435769u;
    return s;
}

float Random(uint seed)
{
    return float(Hash(seed)) / 4294967295.0; // 2^32-1
}
//////

float3 ColorVariation(uint In)
{
    return float3(Random(In), Random(In+100), Random(In+200));
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
    float w = (d00 * d21 - d01 * d20) * denom;
    float u = 1.0 - v - w;
    return float3(u, v, w);
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float2 UV = DispatchRaysIndex().xy / g_sceneCB.ViewRect.zw;

    float3 WorldPos = CalcWorldPositionFromDepth(DepthTexture, AlbedoTextureSampler, UV, g_sceneCB.projectionToWorld);
    //float2 OctNormal = GBuffer0_Normal.SampleLevel(AlbedoTextureSampler, UV, 0);
    //float3 WorldNormal = normalize(DecodeOctNormal(OctNormal));

    //RayDesc ray;
    //ray.Origin = WorldPos;
    //ray.Direction = WorldNormal;

    //ray.TMin = 0.001;
    //ray.TMax = 1.0;

    //RayPayload payload;
    //payload.Lit = 1.0f;
    //TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);

    float z = payload.Lit;
    z = 1.0f;
    RenderTarget[DispatchRaysIndex().xy] = float4(WorldPos, 1);
}

[shader("anyhit")]
void MyAnyHitShader(inout RayPayload payload, in MyAttributes attr)
{

}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{

}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.Lit = 1.0f;
}
