struct VSInput
{
    [[vk::location(0)]] float VertID : POSITION;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    int vert = int(input.VertID);

    output.TexCoord = float2((vert << 1) & 2, vert & 2);  // CCW
    //output.TexCoord = float2(vert & 2, (vert << 1) & 2);    // CW
    output.Pos = float4(output.TexCoord * float2(2.0, 2.0) - float2(1.0, 1.0), 0.5, 1.0);
    
// todo VULKAN_NDC_Y_FLIP
    output.TexCoord.y = 1.0 - output.TexCoord.y;

    return output;
}
