#include "common.hlsl"

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float3 cameraPosition;
    float focalDistance;
    float3 cameraDirection;
    float lensRadius;
};

struct MaterialUniformBuffer
{
    float3 baseColor;
    float anisotropic;

    float3 emission;
    float padding1;

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

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u1, space0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b2, space0);

StructuredBuffer<uint2> VertexIndexOffsetArray[] : register(t0, space1);
StructuredBuffer<uint> IndexBindlessArray[] : register(t0, space2);
StructuredBuffer<RenderObjectUniformBuffer> RenderObjParamArray[] : register(t0, space3);
ByteAddressBuffer VerticesBindlessArray[] : register(t0, space4);
ConstantBuffer<MaterialUniformBuffer> MaterialBindlessArray[] : register(b0, space5);

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

float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

void GetShaderBindingResources(
    inout StructuredBuffer<uint2> VertexIndexOffset,
    inout StructuredBuffer<uint> IndexBindless,
    inout StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam,
    inout ByteAddressBuffer VerticesBindless,
    inout ConstantBuffer<MaterialUniformBuffer> MaterialBindless,
    in uint InstanceIdx)
{
    VertexIndexOffset = VertexIndexOffsetArray[InstanceIdx];
    IndexBindless = IndexBindlessArray[InstanceIdx];
    RenderObjParam = RenderObjParamArray[InstanceIdx];
    VerticesBindless = VerticesBindlessArray[InstanceIdx];
    MaterialBindless = MaterialBindlessArray[InstanceIdx];
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
    uint RecursionDepth;
};

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 origin = 0;
    float3 rayDir = 0;
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;

    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    RayPayload payload = { float4(0.0f, 0.0f, 0.0f, 1.0f), 1 };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);

    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
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

    payload.color = float4(MaterialBindless.baseColor * HitWorldPosition().z, 1);
    //payload.color = float4(HitWorldPosition().z, 0, 0, 1);
}

[shader("anyhit")]
void MyAnyHitShader(inout RayPayload payload, in MyAttributes attr)
{
    payload.color = float4(0, 1, 0, 1);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    //payload.color = float4(0, 0, 1, 1);
}
