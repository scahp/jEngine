
#ifndef USE_VARIABLE_SHADING_RATE
#define USE_VARIABLE_SHADING_RATE 0
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float4 Color : COLOR0;
    [[vk::location(1)]] float2 TexCoord : TEXCOORD0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float4 ShadowPosition : TEXCOORD1;
};

struct DirectionalLightUniformBuffer
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

struct RenderObjectUniformBuffer
{
    float4x4 M;
    float4x4 MV;
    float4x4 MVP;
    float4x4 InvM;
};

cbuffer DirectionalLight : register(b0,space0) { DirectionalLightUniformBuffer DirectionalLight; }
cbuffer RenderObjectParam : register(b0,space1) { RenderObjectUniformBuffer RenderObjectParam; }

Texture2D DirectionalLightShadowMap : register(t1, space0);
SamplerState shadowMapSampler : register(s1, space0);

struct PushConsts
{
    float4 color;
    bool ShowVRSArea;
};
[[vk::push_constant]] PushConsts pushConsts;

float4 main(VSOutput input
#if USE_VARIABLE_SHADING_RATE
, uint shadingRate : SV_ShadingRate
#endif
) : SV_TARGET
{
    float lit = 1.0;
    if (-1.0 <= input.ShadowPosition.z && input.ShadowPosition.z <= 1.0)
	{
        float shadowMapDist = DirectionalLightShadowMap.Sample(shadowMapSampler, input.ShadowPosition.xy * 0.5 + 0.5).r;
        if (input.ShadowPosition.z > shadowMapDist + 0.001)
		{
			lit = 0.5;
		}
	}

    float Intensity = dot(input.Normal, -DirectionalLight.Direction) * lit;
    float4 color = float4(pushConsts.color.rgb * input.Color.xyz * Intensity, 1.0);
    
#if USE_VARIABLE_SHADING_RATE
    if (pushConsts.ShowVRSArea)
    {
        const uint SHADING_RATE_PER_PIXEL = 0x0;
        const uint SHADING_RATE_PER_2X1_PIXELS = 6;
        const uint SHADING_RATE_PER_1X2_PIXELS = 7;
        const uint SHADING_RATE_PER_2X2_PIXELS = 8;
        const uint SHADING_RATE_PER_4X2_PIXELS = 9;
        const uint SHADING_RATE_PER_2X4_PIXELS = 10;
    
        switch (shadingRate)
        {
            case SHADING_RATE_PER_PIXEL:
                return color * float4(0.0, 0.8, 0.4, 1.0);
            case SHADING_RATE_PER_2X1_PIXELS:
                return color * float4(0.2, 0.6, 1.0, 1.0);
            case SHADING_RATE_PER_1X2_PIXELS:
                return color * float4(0.0, 0.4, 0.8, 1.0);
            case SHADING_RATE_PER_2X2_PIXELS:
                return color * float4(1.0, 1.0, 0.2, 1.0);
            case SHADING_RATE_PER_4X2_PIXELS:
                return color * float4(0.8, 0.8, 0.0, 1.0);
            case SHADING_RATE_PER_2X4_PIXELS:
                return color * float4(1.0, 0.4, 0.2, 1.0);
            default:
                return color * float4(0.8, 0.0, 0.0, 1.0);
        }
    }
#endif
    
    return color;
}
