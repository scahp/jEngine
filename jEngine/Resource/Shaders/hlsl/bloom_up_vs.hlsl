struct VSInput
{
    [[vk::location(0)]] float VertID : POSITION0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 TexCoord[8] : TEXCOORD0;
};

// Point on circle.
float2 Circle(float Start, float Points, float Point)
{
    float Rad = (3.141592 * 2.0 * (1.0 / Points)) * (Point + Start);
    return float2(sin(Rad), cos(Rad));
}

struct BloomUniformBuffer
{
    float4 BufferSizeAndInvSize;
    float4 TintA;
    float4 TintB;
    float BloomIntensity;
};

cbuffer BloomParam : register(b0, space0) { BloomUniformBuffer BloomParam; }

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    int vert = int(input.VertID);

    float2 TexCoord = float2((vert << 1) & 2, vert & 2);
    output.Pos = float4(TexCoord * float2(2.0, 2.0) - float2(1.0, 1.0), 0.5, 1.0);
    
// todo VULKAN_NDC_Y_FLIP
    TexCoord.y = 1.0 - TexCoord.y;

    float Start = 2.0 / 7.0;
    float Scale = 0.66f * 2.0f;

    output.TexCoord[0].xy = TexCoord + Circle(Start, 7.0, 0.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[0].zw = TexCoord + Circle(Start, 7.0, 1.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[1].xy = TexCoord + Circle(Start, 7.0, 2.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[1].zw = TexCoord + Circle(Start, 7.0, 3.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[2].xy = TexCoord + Circle(Start, 7.0, 4.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[2].zw = TexCoord + Circle(Start, 7.0, 5.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[3].xy = TexCoord + Circle(Start, 7.0, 6.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;

    output.TexCoord[3].zw = TexCoord;

    Start = 2.0 / 7.0;
    Scale = 0.66f * 2.0f;

    output.TexCoord[4].xy = TexCoord + Circle(Start, 7.0, 0.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[4].zw = TexCoord + Circle(Start, 7.0, 1.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[5].xy = TexCoord + Circle(Start, 7.0, 2.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[5].zw = TexCoord + Circle(Start, 7.0, 3.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[6].xy = TexCoord + Circle(Start, 7.0, 4.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[6].zw = TexCoord + Circle(Start, 7.0, 5.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[7].xy = TexCoord + Circle(Start, 7.0, 6.0) * Scale * BloomParam.BufferSizeAndInvSize.zw;
    output.TexCoord[7].zw = float2(0.0, 0.0);

    return output;
}
