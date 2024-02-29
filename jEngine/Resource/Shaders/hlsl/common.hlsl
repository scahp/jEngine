#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559
#define INV_PI 0.31830988618379067153776752674503

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

float3 Reflect(float3 v, float3 n)
{
    return v - 2 * dot(v, n) * n;
}

#define VERTEX_STRID 56
struct jVertex
{
	float3 Pos;
	float3 Normal;
	float3 Tangent;
	float3 Bitangent;
	float2 TexCoord;
};

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

jVertex GetVertex(in ByteAddressBuffer buffer, uint index)
{
    uint address = index * VERTEX_STRID;
    
    jVertex r;
    r.Pos = asfloat(buffer.Load3(address));
    r.Normal = asfloat(buffer.Load3(address + 12));
    r.Tangent = asfloat(buffer.Load3(address + 24));
    r.Bitangent = asfloat(buffer.Load3(address + 36));
    r.TexCoord = asfloat(buffer.Load2(address + 48));
    return r;
}

//////////////////////////////////////////////////
// https://www.shadertoy.com/view/Nsf3Ws
void RandomHash(inout uint seed)
{
    seed ^= 2747636419u;
    seed *= 2654435769u;
    seed ^= seed >> 16;
    seed *= 2654435769u;
    seed ^= seed >> 16;
    seed *= 2654435769u;
}

uint InitRandomSeed(uint2 InPixelPos, uint2 InResolution, uint InFrameNumber)
{
    return (InPixelPos.y * InResolution.x + InPixelPos.x) + (InFrameNumber * InResolution.x * InResolution.y);
}

uint Random(inout uint seed)
{
    RandomHash(seed);
    return seed;
}

float Random_0_1(inout uint seed)
{
    RandomHash(seed);
    return seed / 4294967295.0;
}
//////////////////////////////////////////////////

// Sampling utility
// References
//  - https://pbr-book.org/4ed/Sampling_Algorithms/Sampling_Multidimensional_Functions#SampleCosineHemisphere
//  - https://github.com/knightcrawler25/GLSL-PathTracer/blob/master/src/shaders/common/disney.glsl
//  - https://github.com/phgphg777/DXR-PathTracer/blob/master/DXRPathTracer/sampling.hlsli
// Sampling cosine weighted vector in Hemisphere
float3 CosWeightedSampleHemisphere(inout uint RandomSeed)
{
    float rand1 = Random_0_1(RandomSeed);
    float rand2 = Random_0_1(RandomSeed);
    
    float3 SampleDir = 0;
    
    // Sampling on disk
    float r = sqrt(rand1); // for cosine weight. - check out the grpah of sqrt(x)
    float phi = TWO_PI * rand2;
    SampleDir.x = r * cos(phi);
    SampleDir.y = r * sin(phi);
    
    // Then z is evaluated by using 'Pythagorean theorem'.
    SampleDir.z = sqrt(max(0.0f, 1.0f - r * r));
    return SampleDir;
}

float3 UniformSampleHemisphere(inout uint RandomSeed)
{
    float rand1 = Random_0_1(RandomSeed);
    float rand2 = Random_0_1(RandomSeed);
    
    float3 SampleDir = 0;
    
    // Sampling on disk
    float r = rand1;
    float phi = TWO_PI * rand2;
    SampleDir.x = r * cos(phi);
    SampleDir.y = r * sin(phi);
    
    // Then z is evaluated by using 'Pythagorean theorem'.
    SampleDir.z = sqrt(max(0.0f, 1.0f - r * r));
    return SampleDir;
}

// Transfrom from world to shading space
float3 ToLocal(float3 T, float3 B, float3 N, float3 WorldVec)
{
    return float3(dot(WorldVec, T), dot(WorldVec, B), dot(WorldVec, N));
}

// Transform from shading to world space
float3 ToWorld(float3 T, float3 B, float3 N, float3 LocalVec)
{
    return LocalVec.x * T + LocalVec.y * B + LocalVec.z * N;
}

// Make T, B vectors from Normal vector
void MakeTB_From_N(in float3 N, inout float3 T, inout float3 B)
{
    float3 up = abs(N.z) < 0.9999999 ? float3(0, 0, 1) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

// Transfrom from world to shading space
float3 ToLocal(in float3 WorldN)
{
    float3 T, B;
    MakeTB_From_N(WorldN, T, B);
    return ToLocal(T, B, WorldN, WorldN);
}

// Transform from shading to world space
float3 ToWorld(in float3 WorldN, in float3 LocalN)
{
    float3 T, B;
    MakeTB_From_N(WorldN, T, B);
    return ToWorld(T, B, WorldN, LocalN);
}
