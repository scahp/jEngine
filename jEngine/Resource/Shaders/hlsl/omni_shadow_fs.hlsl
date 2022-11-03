struct GSOutput
{
    float4 Pos : SV_POSITION;
    float4 WorldPos : TEXCOORD0;
    int Layer : SV_RenderTargetArrayIndex;
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

cbuffer PointLight : register(b0, space0) { jPointLightUniformBufferData PointLight; }

void main(GSOutput input, out float OutDepth : SV_Depth) : SV_TARGET
{
    float lightDistance = length(input.WorldPos.xyz - PointLight.Position);
    OutDepth = lightDistance / PointLight.MaxDistance;
}
