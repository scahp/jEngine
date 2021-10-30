struct ModelViewProjection
{
    matrix MVP;
};

// ConstantBuffer<ModelViewProjection> ModelViewProjectionCB : register(b0);
cbuffer MVPConstantBuffer : register(b0)
{
    matrix MVP;
};

struct VertexPosColor
{
    float3 Position : POSITION;
    float3 Color : COLOR;
    float2 UV : TEXCOORD;
};

struct VertexShaderOutput
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;

    OUT.Position = mul(MVP, float4(IN.Position, 1.0f));
    OUT.Color = float4(IN.Color, 1.0f);
    OUT.UV = IN.UV;
    
    return OUT;
}