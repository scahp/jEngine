struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float4 Color : COLOR0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float3 Tangent : NORMAL1;
    [[vk::location(4)]] float2 TexCoord : TEXCOORD0;
    
    // instancing data
    [[vk::location(5)]] float4 InstancingColor : TEXCOORD1;
    [[vk::location(6)]] float3 InstancingWorld : TEXCOORD2;
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

cbuffer ViewParam : register(b0,space0) { ViewUniformBuffer ViewParam; }
cbuffer DirectionalLight : register(b0,space1) { DirectionalLightUniformBuffer DirectionalLight; }
cbuffer RenderObjectParam : register(b0,space2) { RenderObjectUniformBuffer RenderObjectParam; }

struct VSOutput
{
    float4 Pos : SV_POSITION;
    [[vk::location(0)]] float4 Color : COLOR0;
    [[vk::location(1)]] float2 TexCoord : TEXCOORD0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float4 ShadowPosition : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput) 0;
    
    float3 pos = input.Position + input.InstancingWorld;
    
    output.Pos = mul(ViewParam.VP, mul(RenderObjectParam.M, float4(pos, 1.0)));
    output.Color = input.InstancingColor;
    //output.Color = input.Color;
    output.TexCoord = input.TexCoord;

    output.Normal = normalize(mul((float3x3)RenderObjectParam.M, input.Normal));
	
    output.ShadowPosition = mul(DirectionalLight.ShadowVP, mul(RenderObjectParam.M, float4(pos, 1.0)));
    output.ShadowPosition /= output.ShadowPosition.w;
    return output;
}
