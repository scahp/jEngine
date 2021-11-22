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

// 3���� 16��Ʈ �ε����� byte address buffer �� ���� ������
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAddressBuffer�� 4 ����Ʈ ���ĵǾ�߸� ��.
    // 16 ��Ʈ 3���� indices�� �о�� �ϱ� ������ {0, 1, 2}
    // 4 ����Ʈ ���ĵ� ���� {0 1} {2 0} {1 2} {0 1} ...
    // 2���� �ε����� ������ ó���ϱ� ���ؼ� 8 ����Ʈ�� ���� ���Դϴ�. (~ 4 �ε��� {a b | c d})
    // ù��° �ε����� offsetbyte�� 4 ����Ʈ ��迡�� ���ĵǴ��� ���θ� ������� ��.
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

// hit world position ���
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// �浹�� ���� barycentrics�� ����Ͽ� vertex attributes �� ���� ������ hit position���� attribute�� ����
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] + attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// ī�޶� �ȼ��� ���� ������������� �������� ������, ī�޶� �ȼ��� dispatched 2d Grid�� ������ �ε����� ��ġ��.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f;   // �ȼ��� �߾�
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0f - 1.0f;

    // DirectX ��Ÿ���� ��ǥ�踦 ���� Y�� ������
    screenPos.y = -screenPos.y;

    // �ȼ� ��ǥ�� �� �����Ͽ� �������� ����
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);
}

// Diffuse lighting calculation
float4 CalculationDiffuseLighting(float3 hitPosition, float3 normal)
{
    float3 pixelToLight = normalize(g_sceneCB.lightPosition.xyz - hitPosition);

    // Diffuse �⿩
    float fNDotL = max(0.0f, dot(pixelToLight, normal));
    return g_cubeCB.albedo * g_sceneCB.lightDiffuseColor * fNDotL;
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;

    // ������ ����
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // ������ ����
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;

    // TMin�� 0�� �ƴ� ���� ������ �����Ͽ� �ٸ���� �̽��� ����. - floating point ����
    // TMin�� ���� ���·� �����ؼ� �����ϰ� �ִ� �������� ������Ʈ�� missing�� ����
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    // ��� �ؽ��Ŀ� ������ ������ ������ �����
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    // �ﰢ���� ù��° 16 ��Ʈ �ε����� ��� �ε����� ������
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
    uint baseIndex = PrimitiveIndex() * triangleIndexStride;

    // �ﰢ���� ���� 3���� 16 ��Ʈ �ε����� �ε�
    const uint3 indices = Load3x16BitIndices(baseIndex);

    // �ﰢ�� ���ý��� ���ý� ����� ����
    float3 vertexNormals[3] = {
        Vertices[indices[0]].normal,
        Vertices[indices[1]].normal,
        Vertices[indices[2]].normal
    };

    // �ﰢ���� ����� �����
    // �̰��� �ߺ��̸�, ������ ���ؼ� ������ �ֳ��ϸ� ��� ���ý��� ����� �����ϰ� �� ������ �ﰢ���� ��ְ� ��ġ�ϱ� ����
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

#endif // RAYTRACING_HLSL