
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
};
[[vk::push_constant]] PushConsts pushConsts;

float4 main(VSOutput input) : SV_TARGET
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
    return float4(pushConsts.color.rgb * input.Color.xyz * Intensity, 1.0);
}
