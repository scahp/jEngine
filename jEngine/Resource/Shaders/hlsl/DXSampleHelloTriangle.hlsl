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
    float4 color : COLOR;
};

struct TransformBuffer
{
    float4x4 World;
};

cbuffer TransformParam : register(b0, space0) { TransformBuffer TransformParam; }

PSInput VSMain(float3 position : POSITION, float3 normal : NORMAL)
{
    PSInput result;

    result.position = mul(float4(position, 1.0f), TransformParam.World);
    //result.position = float4(position + TransformParam.World._41_42_43, 1.0);
    //result.position = float4(position, 1.0f);
    result.color = float4(normal, 1.0);

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
