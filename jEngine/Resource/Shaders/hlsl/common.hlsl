#define PI 3.1415926535897932384626433832795028841971693993751058209749

struct RenderObjectUniformBuffer
{
    float4x4 M;
    float4x4 InvM;
    float Metallic;
    float Roughness;
    float padding0;
    float padding1;
};

struct ViewUniformBuffer
{
    float4x4 V;
    float4x4 P;
    float4x4 VP;
    float4x4 InvVP;
    float4x4 PrevVP;
    float3 EyeWorld;
    float padding0;
    float4 ScreenRect;
};

struct jDirectionalLightUniformBuffer
{
    float3 Direction;
    float SpecularPow;

    float3 Color;
    float padding0;

    float3 DiffuseIntensity;
    float padding1;

    float3 SpecularIntensity;
    float padding2;

    float4x4 ShadowVP;
    float4x4 ShadowV;
		
    float3 LightPos;
    float padding3;

    float2 ShadowMapSize;
    float Near;
    float Far;
};

struct jPointLightUniformBufferData
{
    float3 Position;
    float SpecularPow;

    float3 Color;
    float MaxDistance;

    float3 DiffuseIntensity;
    float padding0;

    float3 SpecularIntensity;
    float padding1;

    float4x4 ShadowVP[6];
};

struct jSpotLightUniformBufferData
{
    float3 Position;
    float MaxDistance;

    float3 Direction;
    float PenumbraRadian;

    float3 Color;
    float UmbraRadian;

    float3 DiffuseIntensity;
    float SpecularPow;

    float3 SpecularIntensity;
    float padding0;

    float4x4 ShadowVP;
};

// Octahedron-normal vectors
// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0);
}
 
float2 EncodeOctNormal(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = select(n.z >= 0.0, n.xy, OctWrap(n.xy));
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}
 
float3 DecodeOctNormal(float2 f)
{
    f = f * 2.0 - 1.0;
     
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}
//////////////////////////////////////////

float EncodeHalf2ToFloat(half2 val)
{
    uint2 valInt = uint2(asuint(val.x), asuint(val.y));
    uint encoded = (valInt.x << 16) | (valInt.y & 0xFFFF);
    return asfloat(encoded);
}

half2 DecodeFloatToHalf2(float val)
{
    uint encoded = asuint(val);
    half x = asfloat((encoded >> 16) & 0xFFFF);
    half y = asfloat(encoded & 0xFFFF);
    return half2(x, y);
}

float3 CalcWorldPositionFromDepth(float InDepth, float2 InUV, float4x4 InInvViewProj)
{
    InUV.y = 1.0f - InUV.y;
    
    float4 screenSpacePos;
    screenSpacePos.xy = (InUV * 2.0 - 1.0); // UV range to [-1, 1]
    screenSpacePos.z = InDepth;
    screenSpacePos.w = 1.0;

    // To world space
    float4 worldPos = mul(InInvViewProj, screenSpacePos);
    worldPos.xyz /= worldPos.w;

    return worldPos.xyz;
}

float3 CalcWorldPositionFromDepth(
    Texture2D InDepthTexture, SamplerState InDepthSamplerState, float2 InUV, float4x4 InInvViewProj)
{
#if PIXEL_SHADER
    float DepthValue = InDepthTexture.Sample(InDepthSamplerState, InUV, 0).x;
#else // PIXEL_SHADER
    float DepthValue = InDepthTexture.SampleLevel(InDepthSamplerState, InUV, 0).x;
#endif // PIXEL_SHADER

    return CalcWorldPositionFromDepth(DepthValue, InUV, InInvViewProj);
}

float3 CalcViewPositionFromDepth(float InDepth, float2 InUV, float4x4 InInvViewProj)
{
    return CalcWorldPositionFromDepth(InDepth, InUV, InInvViewProj);
}

float3 CalcViewPositionFromDepth(
    Texture2D InDepthTexture, SamplerState InDepthSamplerState, float2 InUV, float4x4 InInvViewProj)
{
    return CalcWorldPositionFromDepth(InDepthTexture, InDepthSamplerState, InUV, InInvViewProj);
}
