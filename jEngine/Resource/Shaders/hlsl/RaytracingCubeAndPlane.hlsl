#define MAX_RECURSION_DEPTH 10

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float4 cameraPosition;
    float4 lightPosition;
    float4 lightAmbientColor;
    float4 lightDiffuseColor;
    uint NumOfStartingRay;
    float focalDistance;
    float lensRadius;
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

inline float3 GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f;
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0f - 1.0f;

    screenPos.y = -screenPos.y;

    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);

    return world.xyz;
}

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
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);
    color += payload.color;

    // Linear to sRGB
    //color = float4(sqrt(color.xyz), 1.0f);

    //color.xyz = rayDir;

    // RenderTarget[DispatchRaysIndex().xy] = color;

    RWTexture2D<float4> RenderTargetBindless = ResourceDescriptorHeap[0];
    RenderTargetBindless[DispatchRaysIndex().xy] = color;
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

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    uint InstanceIdx = InstanceIndex();

    uint Stride = 5;

    // SRV_UAV DescHeap
    StructuredBuffer<uint2> VertexIndexOffset = ResourceDescriptorHeap[1 + InstanceIdx * Stride];
    StructuredBuffer<uint> IndexBindless = ResourceDescriptorHeap[2 + InstanceIdx * Stride];
    StructuredBuffer<float3> NormalBindless = ResourceDescriptorHeap[3 + InstanceIdx * Stride];
    StructuredBuffer<float2> TexCoordBindless = ResourceDescriptorHeap[4 + InstanceIdx * Stride];
    Texture2D<float4> AlbedoTexture = ResourceDescriptorHeap[5 + InstanceIdx * Stride];

    // Sampler DescHeap
    SamplerState AlbedoTextureSampler = SamplerDescriptorHeap[0];

    uint VertexOffset = VertexIndexOffset[0].x;
    uint IndexOffset = VertexIndexOffset[0].y;

    uint PrimIdx = PrimitiveIndex();
    uint3 Indices = uint3(
        IndexBindless[IndexOffset + PrimIdx * 3],
        IndexBindless[IndexOffset + PrimIdx * 3 + 1],
        IndexBindless[IndexOffset + PrimIdx * 3 + 2]
        );

    float3 Normals[3] = {
        NormalBindless[Indices.x + VertexOffset],
        NormalBindless[Indices.y + VertexOffset],
        NormalBindless[Indices.z + VertexOffset]
    };

    float3 normal = HitAttribute(Normals, attr);
    
    float2 UVs[3] = {
        TexCoordBindless[Indices.x + VertexOffset],
        TexCoordBindless[Indices.y + VertexOffset],
        TexCoordBindless[Indices.z + VertexOffset]
    };

    float2 uv = HitAttribute(UVs, attr);

    float2 ddx = float2(0, 0);
    float2 ddy = float2(0, 0);
    float4 albedo = AlbedoTexture.SampleGrad(AlbedoTextureSampler, uv, ddx, ddy);
    payload.color = albedo;
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
