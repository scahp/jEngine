struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float4 Color : COLOR0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float3 Tangent : NORMAL1;
    [[vk::location(4)]] float2 TexCoord : TEXCOORD0;
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

struct ViewUniformBuffer
{
    float4x4 V;
    float4x4 P;
    float4x4 VP;
    float3 EyeWorld;
    float padding0;
};

struct RenderObjectUniformBuffer
{
    float4x4 M;
    float4x4 InvM;
};

cbuffer ViewParam : register(b0, space0) { ViewUniformBuffer ViewParam; }

cbuffer DirectionalLight : register(b0, space1) { jDirectionalLightUniformBuffer DirectionalLight; }
Texture2D DirectionalLightShadowMap : register(t1, space1);
SamplerState DirectionalLightShadowMapSampler : register(s1, space1);

//cbuffer PointLight : register(b0, space2) { jPointLightUniformBufferData PointLight; }
//TextureCube PointLightShadowCubeMap : register(t1, space2);
//SamplerState PointLightShadowMapSampler : register(s1, space2);

cbuffer SpotLight : register(b0, space3) { jSpotLightUniformBufferData SpotLight; }
TextureCube SpotLightShadowCubeMap : register(t1, space3);
SamplerState SpotLightShadowMapSampler : register(s1, space3);

cbuffer RenderObjectParam : register(b0, space4) { RenderObjectUniformBuffer RenderObjectParam; }


struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL0;
    float4 DirectionalLightShadowPosition : TEXCOORD1;
    float4 SpotLightShadowPosition : TEXCOORD2;
    float4 WorldPos : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    
    output.WorldPos = mul(RenderObjectParam.M, float4(input.Position, 1.0));
    output.Pos = mul(ViewParam.VP, output.WorldPos);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;

    output.Normal = normalize(mul((float3x3)RenderObjectParam.M, input.Normal));

    output.DirectionalLightShadowPosition = mul(DirectionalLight.ShadowVP, output.WorldPos);
    output.SpotLightShadowPosition = mul(SpotLight.ShadowVP, output.WorldPos);
    return output;
}
