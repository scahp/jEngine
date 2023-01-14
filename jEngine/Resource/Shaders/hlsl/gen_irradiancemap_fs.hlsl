#include "common.hlsl"

struct GSOutput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    min16uint LayerIndex : SV_RenderTargetArrayIndex;
};

struct FSOutput
{
    float4 Color : SV_TARGET0;
};

// TextureCube EnvironmentMap : register(t0, space0);
// SamplerComparisonState EnvironmentMapSampler : register(s0, space0);

#define TWO_MIRROR_BALL_SPHERE_MAP 1
#if TWO_MIRROR_BALL_SPHERE_MAP
Texture2D EnvironmentMapTexture : register(t0, space0);
SamplerState EnvironmentMapSampler : register(s0, space0);

// https://www.pauldebevec.com/Probes/
float2 GetSphericalMap_TwoMirrorBall(float3 InDir)
{
    // Convert from Direction3D to UV[-1, 1]
    // - Direction : (Dx, Dy, Dz)
    // - r=(1/pi)*acos(Dz)/sqrt(Dx^2 + Dy^2)
    // - (Dx*r,Dy*r)
    //
    // To rerange UV from [-1, 1] to [0, 1] below explanation with code needed.
    // 0.159154943 == 1.0 / (2.0 * PI)
    // Original r is "r=(1/pi)*acos(Dz)/sqrt(Dx^2 + Dy^2)"
    // To adjust number's range from [-1.0, 1.0] to [0.0, 1.0] for TexCoord, we need this "[-1.0, 1.0] / 2.0 + 0.5"
    // - This is why we use (1.0 / 2.0 * PI) instead of (1.0 / PI) in "r".
    // - adding 0.5 execute next line. (here : "float u = 0.5 + InDir.x * r" and "float v = 0.5 + InDir.y * r")
    float d = sqrt(InDir.x * InDir.x + InDir.y * InDir.y);
    float r = (d > 0.0) ? (0.159154943 * acos(InDir.z) / d) : 0.0;
    float u = 0.5 + InDir.x * r;
    float v = 0.5 + InDir.y * r;
    return float2(u, v);
}

float3 GetNormalFromTexCoord_TwoMirrorBall(float2 InTexCoord)
{
    // Reverse transform from UV[0, 1] to Direction3D.
    // Explanation of equation. scahp(scahp@naver.com)
    // 1. x = ((u - 0.5) / r) from "float u = 0.5 + x * r;"
    // 2. y = ((v - 0.5) / r) from "float v = 0.5 + y * r;"
    // 3. d = sqrt(((u - 0.5) / r)^2 + ((v - 0.5) / r))
    // 4. z = cos(r * d / 0.159154943)
    //  -> r * d is "sqrt((u - 0.5)^2 + (v - 0.5)^2)"
    // 5. Now we get z from z = cos(sqrt((u - 0.5)^2 + (v - 0.5)^2) / 0.159154943)
    // 6. d is sqrt(1.0 - z^2), exaplanation is below.
    //  - r is length of direction. so r length is 1.0
    //  - so r^2 = (x)^2 + (y)^2 + (z)^2 = 1.0
    //  - r^2 = d^2 + (z)^2 = 1.0
    //  - so, d is sqrt(1.0 - z^2)
    // I substitute sqrt(1.0 - z^2) for d in "z = cos(r * d / 0.159154943)"
    //  - We already know z, so we can get r from "float r = 0.159154943 * acos(z) / sqrt(1.0 - z * z);"
    // 7. Now we can get x, y from "x = ((u - 0.5) / r)" and "y = ((v - 0.5) / r)"

    float u = InTexCoord.x;
    float v = InTexCoord.y;

    float z = cos(sqrt((u - 0.5) * (u - 0.5) + (v - 0.5) * (v - 0.5)) / 0.159154943);
    float r = 0.159154943 * acos(z) / sqrt(1.0 - z * z);
    float x = (u - 0.5) / r;
    float y = (v - 0.5) / r;
    return float3(x, y, z);
}
#endif

FSOutput main(GSOutput input) : SV_TARGET
{
    FSOutput output = (FSOutput)0;

//    float3 WorldNormal = normalize(input.Pos.xyz);
//    // output.Color = EnvironmentMap.Sample(EnvironmentMapSampler, WorldNormal);
//
//    float3 up = float3(0.0f, 1.0f, 0.0f);
//    float3 right = normalize(cross(up, WorldNormal));
//    up = normalize(cross(WorldNormal, right));
//
//    float3 irradiance = 0.0f;
//
//    float SampleDelta = 0.025;
//    float NumOfSamples = 0.0;
//    for (float phi = 0.0; phi < 2.0 * PI; phi += NumOfSamples)
//    {
//        for (float theta = 0.0; theta < 0.5 * PI; theta += NumOfSamples)
//        {
//            // spherical to cartesian (in tangent space)
//            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
//            
//            // tangent space to world
//            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * WorldNormal;
//
//            float3 curRGB
//#if TWO_MIRROR_BALL_SPHERE_MAP
//                = EnvironmentMapTexture.Sample(EnvironmentMapSampler, GetSphericalMap_TwoMirrorBall(sampleVec)).rgb;
//#endif
//            irradiance += curRGB * cos(theta) * sin(theta);
//            NumOfSamples++;
//        }
//    }
//
//    output.Color = float4(irradiance, 1.0f);

    float2 Texcoord = GetSphericalMap_TwoMirrorBall(normalize(input.Normal));
    float3 color = EnvironmentMapTexture.Sample(EnvironmentMapSampler, Texcoord).rgb;
    output.Color = float4(color, 1.0f);
    return output;
}
