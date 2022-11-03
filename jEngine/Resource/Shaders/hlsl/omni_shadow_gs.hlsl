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

cbuffer PointLight : register(b0,space0) { jPointLightUniformBufferData PointLight; }

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

struct GSOutput
{
    float4 Pos : SV_POSITION;
    float4 WorldPos : TEXCOORD0;
    int Layer : SV_RenderTargetArrayIndex;
};

[maxvertexcount(18)]
[instance(6)]
void main(triangle VSOutput input[3], inout TriangleStream<GSOutput> outStream)
{
    for (int face = 0; face < 6; ++face)
    {
        for (int i = 0; i < 3; i++)
        {
            float4 tmpPos = input[i].Pos;

            GSOutput output = (GSOutput)0;
            output.Pos = mul(PointLight.ShadowVP[face], tmpPos);
            output.WorldPos = tmpPos;
            output.Layer = face;
            outStream.Append(output);
        }
        outStream.RestartStrip();
    }
}
