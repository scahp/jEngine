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

ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
ConstantBuffer<CubeConstantBuffer> g_cubeCB : register(b1);

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
    return g_cubeCB.albedo * g_sceneCB.lightDiffuseColor * fNDotL;
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;

    // 반직선 생성
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // 반직선 추적
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;

    // TMin을 0이 아닌 작은 값으로 설정하여 앨리어싱 이슈를 피함. - floating point 에러
    // TMin을 작은 갑승로 유지해서 접촉하고 있는 영역에서 지오메트리 missing을 예방
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    // 출력 텍스쳐에 반직선 추적된 색상을 기록함
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
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

    float4 diffuseColor = CalculationDiffuseLighting(hitPosition, triangleNormal);
    float4 color = g_sceneCB.lightAmbientColor + diffuseColor;

    payload.color = color;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float4 background = float4(0.0f, 0.2f, 0.4f, 1.0f);
    payload.color = background;
}

[shader("closesthit")]
void MyPlaneClosestHitShader(inout RayPayload payload, in MyAttributes attr)
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

    float4 diffuseColor = CalculationDiffuseLighting(hitPosition, triangleNormal);
    float4 color = g_sceneCB.lightAmbientColor + diffuseColor;

    payload.color = color;
}

#endif // RAYTRACING_HLSL