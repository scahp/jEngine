//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#define MAX_RECURSION_DEPTH 10

//#include "RaytracingHlslCompat.h"
struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float4 cameraPosition;
    float4 lightPosition;
    float4 lightAmbientColor;
    float4 lightDiffuseColor;
};

struct CubeConstantBuffer
{
    float4 albedo;
};

struct Vertex
{
    float3 position;
    float3 normal;
};

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ByteAddressBuffer Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);
StructuredBuffer<Vertex> PlaneVertices : register(t3, space0);

ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
ConstantBuffer<CubeConstantBuffer> g_localRootSigCB : register(b1);

float3 Reflect(float3 v, float3 n)
{
    return v - 2 * dot(v, n) * n;
}

// 3개의 16비트 인덱스를 byte address buffer 로 부터 가져옴
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAddressBuffer는 4 바이트 정렬되어야만 함.
    // 16 비트 3개의 indices를 읽어야 하기 때문에 {0, 1, 2}
    // 4 바이트 정렬된 것은 {0 1} {2 0} {1 2} {0 1} ...
    // 2개의 인덱스를 세개를 처리하기 위해서 8 바이트를 읽을 것입니다. (~ 4 인덱스 {a b | c d})
    // 첫번째 인덱스의 offsetbyte가 4 바이트 경계에서 정렬되는지 여부를 기반으로 함.
    // Aligned      : {0 1 | 2 -}
    // Not aligned  : {- 0 | 1 2}
    const uint dwordAlignedOffset = offsetBytes & ~3;
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);

    // Aligned : {0 1 | 2 -} => retrieve first three 16 bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else    // Not aligned : {- 0 | 1 2} => retrieve last three 16 bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
    uint currentRecursionDepth;
};

// hit world position 얻기
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// 충돌한 곳의 barycentrics를 사용하여 vertex attributes 로 부터 보간된 hit position에서 attribute를 얻어옴
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// 카메라 픽셀에 대한 월드공간에서의 반직선을 생성함, 카메라 픽셀은 dispatched 2d Grid로 부터의 인덱스와 일치함.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f;   // 픽셀의 중앙
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0f - 1.0f;

    // DirectX 스타일의 좌표계를 위해 Y를 뒤집음
    screenPos.y = -screenPos.y;

    // 픽셀 좌표를 역 투영하여 반직선을 만듬
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);
}

// Diffuse lighting calculation
float4 CalculationDiffuseLighting(float3 hitPosition, float3 normal)
{
    float3 pixelToLight = normalize(g_sceneCB.lightPosition.xyz - hitPosition);

    // Diffuse 기여
    float fNDotL = max(0.0f, dot(pixelToLight, normal));
    return g_localRootSigCB.albedo * g_sceneCB.lightDiffuseColor * fNDotL;
}

float3 random_in_unit_sphere()
{
    float2 uv = DispatchRaysIndex().xy + float2(RayTCurrent(), RayTCurrent() * 2.0f);
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    float noiseZ = (frac(sin(dot(uv, float2(12.9898, 78.233) * 3.0)) * 43758.5453));

    float3 randomUniSphere = float3(noiseX, noiseY, noiseZ) * 2.0f - 0.5f;
    if (length(randomUniSphere) <= 1.0f)
        return randomUniSphere;

    return normalize(randomUniSphere);
}

float3 random_in_hemisphere(float3 normal)
{
    float3 in_unit_sphere = random_in_unit_sphere();
    if (dot(in_unit_sphere, normal) > 0.0)  // 노멀 기준으로 같은 반구 방향인지?
        return in_unit_sphere;

    return -in_unit_sphere;
}

float reflectance(float cosine, float ref_idx) 
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
    float3 cos_theta = min(dot(-uv, normal), 1.0);
    float3 r_out_perp = etai_over_etat * (uv + cos_theta * normal);
    float r_out_perp_length = length(r_out_perp);
    float3 r_out_parallel = -sqrt(abs(1.0 - r_out_perp_length * r_out_perp_length)) * normal;
    return r_out_perp + r_out_parallel;
}

float3 random_in_unit_sphere2(int i)
{
    float2 uv = DispatchRaysIndex().xy + float2(i, i * 2.0f);
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    float noiseZ = (frac(sin(dot(uv, float2(12.9898, 78.233) * 3.0)) * 43758.5453));

    float3 randomUniSphere = float3(noiseX, noiseY, noiseZ) * 2.0f - 0.5f;
    if (length(randomUniSphere) <= 1.0f)
        return randomUniSphere;

    return normalize(randomUniSphere);
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;

    // 반직선 생성
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    int samples = 100;

    // 반직선 추적
    for (int i = 0; i < samples; ++i)
    {
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = rayDir + float3(random_in_unit_sphere2(i).xy / DispatchRaysDimensions().xy, 0.0f);

        // TMin을 0이 아닌 작은 값으로 설정하여 앨리어싱 이슈를 피함. - floating point 에러
        // TMin을 작은 갑승로 유지해서 접촉하고 있는 영역에서 지오메트리 missing을 예방
        ray.TMin = 0.001;
        ray.TMax = 10000.0;

        // RayPayload payload = { float4(0, 0, 0, 0) };
        RayPayload payload = { float4(1.0f, 1.0f, 1.0f, 1.0f), 1 };
        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

        color += payload.color;
    }

    color /= samples;

    // Linear to sRGB
    color = float4(sqrt(color.xyz), 1.0f);

    // 출력 텍스쳐에 반직선 추적된 색상을 기록함
    RenderTarget[DispatchRaysIndex().xy] = color;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();
        
    // 삼각형의 첫번째 16 비트 인덱스를 기반 인덱스로 가져옴
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * triangleIndexStride;

    // 삼각형을 위한 3개의 16 비트 인덱스를 로드
    const uint3 indices = Load3x16BitIndices(baseIndex);

    // 삼각형 버택스의 버택스 노멀을 얻음
    float3 vertexNormals[3] =
    {
        Vertices[indices[0]].normal,
        Vertices[indices[1]].normal,
        Vertices[indices[2]].normal
    };

    // 삼각형의 노멀을 계산함
    // 이것은 중복이며, 설명을 위해서 수행함 왜냐하면 모든 버택스당 노멀은 동일하고 이 샘플의 삼각형의 노멀과 일치하기 때문
    float3 triangleNormal = HitAttribute(vertexNormals, attr);

    ///

    if (payload.currentRecursionDepth < MAX_RECURSION_DEPTH)
    {
        //RayPayload newPayload;
        //newPayload.color = payload.color * g_localRootSigCB.albedo;
        //newPayload.currentRecursionDepth = payload.currentRecursionDepth + 1;

        //// 반직선 추적
        //RayDesc ray;
        //ray.Origin = hitPosition;
        //ray.Direction = triangleNormal + random_in_unit_sphere();

        //// TMin을 0이 아닌 작은 값으로 설정하여 앨리어싱 이슈를 피함. - floating point 에러
        //// TMin을 작은 갑승로 유지해서 접촉하고 있는 영역에서 지오메트리 missing을 예방
        //ray.TMin = 0.001;
        //ray.TMax = 10000.0;
        //TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, newPayload);

        //payload.color *= newPayload.color;

        RayPayload newPayload;
        newPayload.color = payload.color * g_localRootSigCB.albedo;
        newPayload.currentRecursionDepth = payload.currentRecursionDepth + 1;

        // 반직선 추적
        RayDesc ray;
        ray.Origin = hitPosition;
        //ray.Direction = MakeDefaultReflection(triangleNormal);        // 1. Default Reflection
        //ray.Direction = MakeLambertianReflection(triangleNormal);     // 2. Lambertian reflection
        ray.Direction = MakeMirrorReflection(triangleNormal, 0.0f);   // 3. Mirror Reflection
        //ray.Direction = MakeRefract(WorldRayDirection(), triangleNormal, 1.0f/1.5f);

        /*float cos_theta = min(dot(-WorldRayDirection(), triangleNormal), 1.0);
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

        float ir = 1.5f;
        bool IsFrontFace = true;
        float refraction_ratio = IsFrontFace ? (1.0 / ir) : ir;
        bool cannot_refract = refraction_ratio * sin_theta > 1.0;

        if (cannot_refract || reflectance(cos_theta, refraction_ratio) > (random_in_unit_sphere().x * 0.5f + 1.0f))
            ray.Direction = MakeMirrorReflection(triangleNormal);
        else
            ray.Direction = MakeRefract(WorldRayDirection(), triangleNormal, refraction_ratio);*/

        // TMin을 0이 아닌 작은 값으로 설정하여 앨리어싱 이슈를 피함. - floating point 에러
        // TMin을 작은 갑승로 유지해서 접촉하고 있는 영역에서 지오메트리 missing을 예방
        ray.TMin = 0.001;
        ray.TMax = 10000.0;

        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, newPayload);

        payload.color = newPayload.color;
    }
    ///

    //float4 diffuseColor = CalculationDiffuseLighting(hitPosition, triangleNormal);
    //float4 color = g_sceneCB.lightAmbientColor + diffuseColor;

    //payload.color = color;
    ////payload.color = float4(triangleNormal, 1.0);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float3 rayDir;
    float3 origin;

    // 반직선 생성
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    float t = (rayDir.y + 1.0f) * 0.5f;
    float3 background = (1.0f - t) * float3(1.0f, 1.0f, 1.0f) + t * float3(0.5f, 0.7f, 1.0f);

    payload.color *= float4(background, 1.0f);
}

[shader("closesthit")]
void MyPlaneClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    //// 삼각형의 노멀을 계산함
    //// 이것은 중복이며, 설명을 위해서 수행함 왜냐하면 모든 버택스당 노멀은 동일하고 이 샘플의 삼각형의 노멀과 일치하기 때문
    //float3 triangleNormal = HitAttribute(vertexNormals, attr);

    //float4 diffuseColor = CalculationDiffuseLighting(hitPosition, triangleNormal);
    //float4 color = g_sceneCB.lightAmbientColor + diffuseColor;

    float3 vertexNormals[3] =
    {
        PlaneVertices[PrimitiveIndex() * 3 + 0].normal,
        PlaneVertices[PrimitiveIndex() * 3 + 1].normal,
        PlaneVertices[PrimitiveIndex() * 3 + 2].normal
    };
    
    // 삼각형의 노멀을 계산함
    // 이것은 중복이며, 설명을 위해서 수행함 왜냐하면 모든 버택스당 노멀은 동일하고 이 샘플의 삼각형의 노멀과 일치하기 때문
    float3 triangleNormal = HitAttribute(vertexNormals, attr);

    if (payload.currentRecursionDepth < MAX_RECURSION_DEPTH)
    {
        RayPayload newPayload;
        newPayload.color = payload.color * g_localRootSigCB.albedo;
        newPayload.currentRecursionDepth = payload.currentRecursionDepth + 1;

        // 반직선 추적
        RayDesc ray;
        ray.Origin = hitPosition;
        //ray.Direction = MakeDefaultReflection(triangleNormal);            // 1. Default reflection
        ray.Direction = MakeLambertianReflection(triangleNormal);           // 2. Lambertian reflection
        //ray.Direction = MakeMirrorReflection(triangleNormal, 0.0f);       // 3. Mirror Reflection

        // TMin을 0이 아닌 작은 값으로 설정하여 앨리어싱 이슈를 피함. - floating point 에러
        // TMin을 작은 갑승로 유지해서 접촉하고 있는 영역에서 지오메트리 missing을 예방
        ray.TMin = 0.001;
        ray.TMax = 10000.0;
        TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, newPayload);

        payload.color = newPayload.color;
    }

    //float4 diffuseColor = CalculationDiffuseLighting(hitPosition, triangleNormal);
    //float4 color = g_sceneCB.lightAmbientColor + diffuseColor;
    //
    //payload.color = color;
    ////payload.color = float4(triangleNormal, 1.0f);
}

#endif // RAYTRACING_HLSL