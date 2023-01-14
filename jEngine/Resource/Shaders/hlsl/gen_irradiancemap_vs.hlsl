#include "common.hlsl"

struct VSInput
{
    [[vk::location(0)]] float VertID : POSITION0;
    [[vk::location(1)]] min16uint LayerIndex : TEXCOORD0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float3 Normal : NORMAL0;
    min16uint LayerIndex : SV_RenderTargetArrayIndex;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    output.LayerIndex = input.LayerIndex;

    int vert = int(input.VertID);

    float2 VertXY = float2((vert << 1) & 2, vert & 2);
    output.Pos = float4(VertXY * float2(2.0, 2.0) - float2(1.0, 1.0), 0.5, 1.0);

    //VertXY.x = -output.Pos.y;
    //VertXY.y = output.Pos.x;

    // https://en.wikipedia.org/wiki/Cube_mapping
    VertXY = output.Pos.xy;
    if (0 == input.LayerIndex)              // PositiveX
    {
        output.Normal.x = 1.0f;
        //output.Normal.yz = VertXY;
        output.Normal.zy = float2(-VertXY.x, VertXY.y);
    }
    else if (1 == input.LayerIndex)         // NegativeX
    {
        output.Normal.x = -1.0f;
        // output.Normal.yz = VertXY;
        output.Normal.zy = float2(VertXY.x, VertXY.y);
    }
    else if (2 == input.LayerIndex)         // PositiveY
    {
        output.Normal.y = 1.0f;
        //output.Normal.xz = VertXY;
        output.Normal.xz = float2(VertXY.x, -VertXY.y);
    }
    else if (3 == input.LayerIndex)         // NegativeY
    {
        output.Normal.y = -1.0f;
        // output.Normal.xz = VertXY;
        output.Normal.xz = float2(VertXY.x, VertXY.y);
    }
    else if (4 == input.LayerIndex)         // PositiveZ
    {
        output.Normal.z = 1.0f;
        // output.Normal.yx = VertXY;
        output.Normal.xy = float2(VertXY.x, VertXY.y);
    }
    else if (5 == input.LayerIndex)         // NegativeZ
    {
        output.Normal.z = -1.0f;
        // output.Normal.yx = VertXY;
        output.Normal.xy = float2(-VertXY.x, VertXY.y);
    }

    return output;
}
