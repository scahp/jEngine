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

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct TransformBuffer
{
    float4x4 World;
};

ConstantBuffer<TransformBuffer> TransformParam : register(b0, space0);
StructuredBuffer<float4> Colors : register(t0, space0);                                 // StructuredBuffer test
Texture2D SimpleTexture : register(t1, space0);
SamplerState SimpleSamplerState : register(s0, space0);

PSInput VSMain(float3 position : POSITION, float3 normal : NORMAL)
{
    PSInput result;

    result.position = mul(float4(position, 1.0f), TransformParam.World);
    //result.position = float4(position + TransformParam.World._41_42_43, 1.0);
    //result.position = float4(position, 1.0f);
    result.uv = result.position.xy / result.position.w;
    result.color = float4(normal, 1.0) * Colors[0];                                     // StructuredBuffer test

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return SimpleTexture.Sample(SimpleSamplerState, input.uv * 0.5 + 0.5) + input.color;
}