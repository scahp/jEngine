#include "common.hlsl"
#include "PBR.hlsl"

#define MAX_RECURSION_DEPTH 10

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float4 cameraPosition;
    float4 lightDireciton;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);

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
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

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
    inout StructuredBuffer<float3> PosBindless,
    inout StructuredBuffer<float3> NormalBindless,
    inout StructuredBuffer<float3> TangentBindless,
    inout StructuredBuffer<float3> BiTangentBindless,
    inout StructuredBuffer<float2> TexCoordBindless,
    inout Texture2D AlbedoTexture,
    inout Texture2D NormalTexture,
    inout Texture2D RMTexture,
    in uint InstanceIdx)
{
    IrradianceMap = ResourceDescriptorHeap[1];
    PrefilteredEnvMap = ResourceDescriptorHeap[2];

    uint PerPrimOffset = 3;
    uint Stride = 11;

    VertexIndexOffset = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    IndexBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    RenderObjParam = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    PosBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    NormalBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    TangentBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    BiTangentBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    TexCoordBindless = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    AlbedoTexture = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    NormalTexture = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
    RMTexture = ResourceDescriptorHeap[(PerPrimOffset++) + InstanceIdx * Stride];
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

//////////////////////////////////////////
// Second ray type which is the shadow
struct ShadowHitInfo
{
    bool isHit;
};

[shader("closesthit")]
void ShadowMyClosestHitShader(inout ShadowHitInfo payload, in MyAttributes attr)
{
    payload.isHit = true;
}

[shader("anyhit")]
void ShadowMyAnyHitShader(inout ShadowHitInfo payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
    uint InstanceIdx = InstanceIndex();

    // SRV_UAV DescHeap
    TextureCube<float4> IrradianceMap;
    TextureCube<float4> PrefilteredEnvMap;
    StructuredBuffer<uint2> VertexIndexOffset;
    StructuredBuffer<uint> IndexBindless;
    StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam;
    StructuredBuffer<float3> PosBindless;
    StructuredBuffer<float3> NormalBindless;
    StructuredBuffer<float3> TangentBindless;
    StructuredBuffer<float3> BiTangentBindless;
    StructuredBuffer<float2> TexCoordBindless;
    Texture2D AlbedoTexture;
    Texture2D NormalTexture;
    Texture2D RMTexture;

    GetShaderBindingResources(IrradianceMap, 
        PrefilteredEnvMap, 
        VertexIndexOffset,
        IndexBindless,
        RenderObjParam,
        PosBindless,
        NormalBindless,
        TangentBindless,
        BiTangentBindless,
        TexCoordBindless,
        AlbedoTexture,
        NormalTexture,
        RMTexture,
        InstanceIdx);

    // Sampler DescHeap
    SamplerState AlbedoTextureSampler = SamplerDescriptorHeap[0];

    uint VertexOffset = VertexIndexOffset[0].x;
    uint IndexOffset = VertexIndexOffset[0].y;

    uint PrimIdx = PrimitiveIndex();
    uint3 Indices = uint3(
        IndexBindless[IndexOffset + PrimIdx * 3],
        IndexBindless[IndexOffset + PrimIdx * 3 + 1],
        IndexBindless[IndexOffset + PrimIdx * 3 + 2]);

    float2 uv0 = TexCoordBindless[Indices.x + VertexOffset];
    float2 uv1 = TexCoordBindless[Indices.y + VertexOffset];
    float2 uv2 = TexCoordBindless[Indices.z + VertexOffset];

    float2 uv = HitAttribute(uv0 , uv1 , uv2, attr);

    // From D3D12RaytracingMiniEngineSample https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/DiffuseHitShaderLib.hlsl
    //---------------------------------------------------------------------------------------------
    // Compute partial derivatives of UV coordinates:
    //
    //  1) Construct a plane from the hit triangle
    //  2) Intersect two helper rays with the plane:  one to the right and one down
    //  3) Compute barycentric coordinates of the two hit points
    //  4) Reconstruct the UV coordinates at the hit points
    //  5) Take the difference in UV coordinates as the partial derivatives X and Y

    float3 p0 = PosBindless[Indices.x + VertexOffset];
    float3 p1 = PosBindless[Indices.y + VertexOffset];
    float3 p2 = PosBindless[Indices.z + VertexOffset];

    // Normal for plane
    float3 triangleNormal = normalize(cross(p2 - p0, p1 - p0));

    // Helper rays
    uint2 threadID = DispatchRaysIndex().xy;
    float3 ddxOrigin, ddxDir, ddyOrigin, ddyDir;
    GenerateCameraRay(uint2(threadID.x + 1, threadID.y), ddxOrigin, ddxDir);
    GenerateCameraRay(uint2(threadID.x, threadID.y + 1), ddyOrigin, ddyDir);

    // Intersect helper rays
    float3 xOffsetPoint = RayPlaneIntersection(hitPosition, triangleNormal, ddxOrigin, ddxDir);
    float3 yOffsetPoint = RayPlaneIntersection(hitPosition, triangleNormal, ddyOrigin, ddyDir);

    // Compute barycentrics 
    float3 baryX = BarycentricCoordinates(xOffsetPoint, p0, p1, p2);
    float3 baryY = BarycentricCoordinates(yOffsetPoint, p0, p1, p2);

    // Compute UVs and take the difference
    float3x2 uvMat = float3x2(uv0, uv1, uv2);
    float2 ddx = mul(baryX, uvMat) - uv;
    float2 ddy = mul(baryY, uvMat) - uv;

    float4 albedo = AlbedoTexture.SampleGrad(AlbedoTextureSampler, uv, ddx, ddy);
    if (albedo.w < 0.5)
    {
        IgnoreHit();
    }
    else if (albedo.w >= 1.0)
    {
        AcceptHitAndEndSearch();
    }
}

[shader("miss")]
void ShadowMyMissShader(inout ShadowHitInfo payload)
{
    payload.isHit = false;
}
//////////////////////////////////////////

[shader("raygeneration")]
void MyRaygenShader()
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    int samples = 1;

    float3 rayDir;
    float3 origin;
    float3 world = GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;

    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    
    RayPayload payload = { float4(1.0f, 1.0f, 1.0f, 1.0f) };
    ShadowHitInfo shadowPayload;
    shadowPayload.isHit = false;
    
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 0, 0, ray, payload);
    //color = shadowPayload.isHit ? float4(1, 1, 1, 1) : float4(0, 0, 0, 1);
    color = payload.color;
    
    /*
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 1, 0, 1, ray, shadowPayload);
    color.xyz = shadowPayload.isHit ? float4(1, 1, 1, 1) : float4(0, 0, 0, 1);
    */

    RWTexture2D<float4> RenderTargetBindless = ResourceDescriptorHeap[0];
    RenderTargetBindless[DispatchRaysIndex().xy] = color;
}

[shader("anyhit")]
void MyAnyHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
    uint InstanceIdx = InstanceIndex();

    // SRV_UAV DescHeap
    TextureCube<float4> IrradianceMap;
    TextureCube<float4> PrefilteredEnvMap;
    StructuredBuffer<uint2> VertexIndexOffset;
    StructuredBuffer<uint> IndexBindless;
    StructuredBuffer<RenderObjectUniformBuffer> RenderObjParam;
    StructuredBuffer<float3> PosBindless;
    StructuredBuffer<float3> NormalBindless;
    StructuredBuffer<float3> TangentBindless;
    StructuredBuffer<float3> BiTangentBindless;
    StructuredBuffer<float2> TexCoordBindless;
    Texture2D AlbedoTexture;
    Texture2D NormalTexture;
    Texture2D RMTexture;

    GetShaderBindingResources(IrradianceMap, 
        PrefilteredEnvMap, 
        VertexIndexOffset,
        IndexBindless,
        RenderObjParam,
        PosBindless,
        NormalBindless,
        TangentBindless,
        BiTangentBindless,
        TexCoordBindless,
        AlbedoTexture,
        NormalTexture,
        RMTexture,
        InstanceIdx);

    // Sampler DescHeap
    SamplerState AlbedoTextureSampler = SamplerDescriptorHeap[0];

    uint VertexOffset = VertexIndexOffset[0].x;
    uint IndexOffset = VertexIndexOffset[0].y;

    uint PrimIdx = PrimitiveIndex();
    uint3 Indices = uint3(
        IndexBindless[IndexOffset + PrimIdx * 3],
        IndexBindless[IndexOffset + PrimIdx * 3 + 1],
        IndexBindless[IndexOffset + PrimIdx * 3 + 2]);

    float2 uv0 = TexCoordBindless[Indices.x + VertexOffset];
    float2 uv1 = TexCoordBindless[Indices.y + VertexOffset];
    float2 uv2 = TexCoordBindless[Indices.z + VertexOffset];

    float2 uv = HitAttribute(uv0 , uv1 , uv2, attr);

    // From D3D12RaytracingMiniEngineSample https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/DiffuseHitShaderLib.hlsl
    //---------------------------------------------------------------------------------------------
    // Compute partial derivatives of UV coordinates:
    //
    //  1) Construct a plane from the hit triangle
    //  2) Intersect two helper rays with the plane:  one to the right and one down
    //  3) Compute barycentric coordinates of the two hit points
    //  4) Reconstruct the UV coordinates at the hit points
    //  5) Take the difference in UV coordinates as the partial derivatives X and Y

    float3 p0 = PosBindless[Indices.x + VertexOffset];
    float3 p1 = PosBindless[Indices.y + VertexOffset];
    float3 p2 = PosBindless[Indices.z + VertexOffset];

    // Normal for plane
    float3 triangleNormal = normalize(cross(p2 - p0, p1 - p0));

    // Helper rays
    uint2 threadID = DispatchRaysIndex().xy;
    float3 ddxOrigin, ddxDir, ddyOrigin, ddyDir;
    GenerateCameraRay(uint2(threadID.x + 1, threadID.y), ddxOrigin, ddxDir);
    GenerateCameraRay(uint2(threadID.x, threadID.y + 1), ddyOrigin, ddyDir);

    // Intersect helper rays
    float3 xOffsetPoint = RayPlaneIntersection(hitPosition, triangleNormal, ddxOrigin, ddxDir);
    float3 yOffsetPoint = RayPlaneIntersection(hitPosition, triangleNormal, ddyOrigin, ddyDir);

    // Compute barycentrics 
    float3 baryX = BarycentricCoordinates(xOffsetPoint, p0, p1, p2);
    float3 baryY = BarycentricCoordinates(yOffsetPoint, p0, p1, p2);

    // Compute UVs and take the difference
    float3x2 uvMat = float3x2(uv0, uv1, uv2);
    float2 ddx = mul(baryX, uvMat) - uv;
    float2 ddy = mul(baryY, uvMat) - uv;

    float4 albedo = AlbedoTexture.SampleGrad(AlbedoTextureSampler, uv, ddx, ddy);
    if (albedo.w < 0.5)
    {
        IgnoreHit();
    }
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
    StructuredBuffer<float3> PosBindless;
    StructuredBuffer<float3> NormalBindless;
    StructuredBuffer<float3> TangentBindless;
    StructuredBuffer<float3> BiTangentBindless;
    StructuredBuffer<float2> TexCoordBindless;
    Texture2D AlbedoTexture;
    Texture2D NormalTexture;
    Texture2D RMTexture;

    GetShaderBindingResources(IrradianceMap, 
        PrefilteredEnvMap, 
        VertexIndexOffset,
        IndexBindless,
        RenderObjParam,
        PosBindless,
        NormalBindless,
        TangentBindless,
        BiTangentBindless,
        TexCoordBindless,
        AlbedoTexture,
        NormalTexture,
        RMTexture,
        InstanceIdx);

    // Sampler DescHeap
    SamplerState AlbedoTextureSampler = SamplerDescriptorHeap[0];

    uint VertexOffset = VertexIndexOffset[0].x;
    uint IndexOffset = VertexIndexOffset[0].y;

    uint PrimIdx = PrimitiveIndex();
    uint3 Indices = uint3(
        IndexBindless[IndexOffset + PrimIdx * 3],
        IndexBindless[IndexOffset + PrimIdx * 3 + 1],
        IndexBindless[IndexOffset + PrimIdx * 3 + 2]);

    float3 normal = HitAttribute(NormalBindless[Indices.x + VertexOffset]
        , NormalBindless[Indices.y + VertexOffset]
        , NormalBindless[Indices.z + VertexOffset], attr);

    float3 tangent = HitAttribute(TangentBindless[Indices.x + VertexOffset]
        , TangentBindless[Indices.y + VertexOffset]
        , TangentBindless[Indices.z + VertexOffset], attr);
        
    float3 bitangent = HitAttribute(NormalBindless[Indices.x + VertexOffset]
        , BiTangentBindless[Indices.y + VertexOffset]
        , BiTangentBindless[Indices.z + VertexOffset], attr);

    float3x3 TBN = 0;
    {
        float3 T = normalize(mul((float3x3)RenderObjParam[0].M, tangent).xyz);
        float3 B = normalize(mul((float3x3)RenderObjParam[0].M, bitangent).xyz);
        float3 N = normalize(mul((float3x3)RenderObjParam[0].M, normal).xyz);
        TBN = transpose(float3x3(T, B, N));
    }

    float2 uv0 = TexCoordBindless[Indices.x + VertexOffset];
    float2 uv1 = TexCoordBindless[Indices.y + VertexOffset];
    float2 uv2 = TexCoordBindless[Indices.z + VertexOffset];

    float2 uv = HitAttribute(uv0 , uv1 , uv2, attr);

    //float2 ddx = float2(0, 0);
    //float2 ddy = float2(0, 0);

    // From D3D12RaytracingMiniEngineSample https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/src/D3D12RaytracingMiniEngineSample/DiffuseHitShaderLib.hlsl
    //---------------------------------------------------------------------------------------------
    // Compute partial derivatives of UV coordinates:
    //
    //  1) Construct a plane from the hit triangle
    //  2) Intersect two helper rays with the plane:  one to the right and one down
    //  3) Compute barycentric coordinates of the two hit points
    //  4) Reconstruct the UV coordinates at the hit points
    //  5) Take the difference in UV coordinates as the partial derivatives X and Y

    float3 p0 = PosBindless[Indices.x + VertexOffset];
    float3 p1 = PosBindless[Indices.y + VertexOffset];
    float3 p2 = PosBindless[Indices.z + VertexOffset];

    // Normal for plane
    float3 triangleNormal = normalize(cross(p2 - p0, p1 - p0));

    // Helper rays
    uint2 threadID = DispatchRaysIndex().xy;
    float3 ddxOrigin, ddxDir, ddyOrigin, ddyDir;
    GenerateCameraRay(uint2(threadID.x + 1, threadID.y), ddxOrigin, ddxDir);
    GenerateCameraRay(uint2(threadID.x, threadID.y + 1), ddyOrigin, ddyDir);

    // Intersect helper rays
    float3 xOffsetPoint = RayPlaneIntersection(hitPosition, triangleNormal, ddxOrigin, ddxDir);
    float3 yOffsetPoint = RayPlaneIntersection(hitPosition, triangleNormal, ddyOrigin, ddyDir);

    // Compute barycentrics 
    float3 baryX = BarycentricCoordinates(xOffsetPoint, p0, p1, p2);
    float3 baryY = BarycentricCoordinates(yOffsetPoint, p0, p1, p2);

    // Compute UVs and take the difference
    float3x2 uvMat = float3x2(uv0, uv1, uv2);
    float2 ddx = mul(baryX, uvMat) - uv;
    float2 ddy = mul(baryY, uvMat) - uv;

    float4 albedo = AlbedoTexture.SampleGrad(AlbedoTextureSampler, uv, ddx, ddy);
    //payload.color = albedo;

    float3 TexNormal = NormalTexture.SampleGrad(AlbedoTextureSampler, uv, ddx, ddy).xyz;
    TexNormal.y = 1.0 - TexNormal.y;
    TexNormal= TexNormal * 2.0f - 1.0f;
    float3 WorldNormal = normalize(mul(TBN, TexNormal));
    //payload.color = float4(WorldNormal, 1.0);

    float roughness = RMTexture.SampleGrad(AlbedoTextureSampler, uv, ddx, ddy).y;
    float metallic = RMTexture.SampleGrad(AlbedoTextureSampler, uv, ddx, ddy).z;

    float3 L = -g_sceneCB.lightDireciton;
    float3 N = WorldNormal;
    float3 V = -WorldRayDirection();
    const float DistanceToLight = 1.0f;     // Directional light from the Sun is not having attenuation by using distance
    payload.color.xyz = PBR(L, N, V, albedo, float3(1, 1, 1), DistanceToLight, metallic, roughness);
    payload.color.w = 1.0f;

    SamplerState PBRSamplerState = SamplerDescriptorHeap[1];
    // ambient from IBL
    {
        // todo : Need to split shader, because it is possible that ILB without directional light
        float3 DiffusePart = IBL_DiffusePart(N, V, albedo, metallic, roughness, IrradianceMap, PBRSamplerState);

        float NoV = saturate(dot(N, V));
        float3 R = 2 * dot(V, N) * N - V;
        R = normalize(R);
        float3 PrefilteredColor = PrefilteredEnvMap.SampleLevel(PBRSamplerState, R, roughness * (8-1)).rgb;
        
        float3 F0 = float3(0.04f, 0.04f, 0.04f);
        float3 SpecularColor = lerp(F0, albedo, metallic);

        float3 SpecularPart = PrefilteredColor * EnvBRDFApprox(SpecularColor, roughness, NoV);
        payload.color.xyz += (DiffusePart + SpecularPart);
    }

    bool TraceRayForShadow = true;
    if (TraceRayForShadow)
    {
        // Find the world - space hit position
        float3 worldOrigin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

        float3 lightDir = -g_sceneCB.lightDireciton.xyz;

        // Fire a shadow ray. The direction is hard-coded here, but can be fetched
        // from a constant-buffer
        RayDesc ray;
        ray.Origin = worldOrigin;
        ray.Direction = lightDir;
        ray.TMin = 0.01;
        ray.TMax = 100000;
        bool hit = true;

        // Initialize the ray payload
        ShadowHitInfo shadowPayload;
        shadowPayload.isHit = false;

        // Trace the ray
        TraceRay(
            // Acceleration structure
            Scene,
            // Flags can be used to specify the behavior upon hitting a surface
            RAY_FLAG_NONE,
            // Instance inclusion mask, which can be used to mask out some geometry to
            // this ray by and-ing the mask with a geometry mask. The 0xFF flag then
            // indicates no geometry will be masked
            0xFF,
            // Depending on the type of ray, a given object can have several hit
            // groups attached (ie. what to do when hitting to compute regular
            // shading, and what to do when hitting to compute shadows). Those hit
            // groups are specified sequentially in the SBT, so the value below
            // indicates which offset (on 4 bits) to apply to the hit groups for this
            // ray. In this sample we only have one hit group per object, hence an
            // offset of 0.
            1,
            // The offsets in the SBT can be computed from the object ID, its instance
            // ID, but also simply by the order the objects have been pushed in the
            // acceleration structure. This allows the application to group shaders in
            // the SBT in the same order as they are added in the AS, in which case
            // the value below represents the stride (4 bits representing the number
            // of hit groups) between two consecutive objects.
            0,
            // Index of the miss shader to use in case several consecutive miss
            // shaders are present in the SBT. This allows to change the behavior of
            // the program when no geometry have been hit, for example one to return a
            // sky color for regular rendering, and another returning a full
            // visibility value for shadow rays. This sample has only one miss shader,
            // hence an index 0
            1,
            // Ray information to trace
            ray,
            // Payload associated to the ray, which will be used to communicate
            // between the hit/miss shaders and the raygen
            shadowPayload);

        float factor = shadowPayload.isHit ? 0.3 : 1.0;
        payload.color.xyz *= float3(factor, factor, factor);
    }
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
     // Make a 't' value that is the factor scaled by using ray hit on background of Y axis.
    float2 xy = HitWorldPosition().xy;
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0f - 1.0f;;
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);
    world.xyz /= world.w;
    float3 origin = g_sceneCB.cameraPosition.xyz;
    float3 direction = normalize(world.xyz - origin);

    float t = (direction.y + 1.0f) * 0.5f;
    float3 background = (1.0f - t) * float3(1.0f, 1.0f, 1.0f) + t * float3(0.5f, 0.7f, 1.0f);

    payload.color = float4(background, 1.0f);
}
