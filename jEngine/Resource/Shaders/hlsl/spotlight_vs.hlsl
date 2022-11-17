//struct PushConsts
//{
//    float2 Pos;
//    float2 Size;
//    float2 PixelSize;
//    float Depth;
//    float padding;
//};
//[[vk::push_constant]] PushConsts pushConsts;

struct PushConsts
{
    float4x4 MVP;
};
[[vk::push_constant]] PushConsts pushConsts;

struct VSInput
{
    //[[vk::location(0)]] float2 Position : POSITION0;

    [[vk::location(0)]] float3 Position : POSITION0;
    [[vk::location(1)]] float4 Color : COLOR0;
    [[vk::location(2)]] float3 Normal : NORMAL0;
    [[vk::location(3)]] float3 Tangent : NORMAL1;
    [[vk::location(4)]] float2 TexCoord : TEXCOORD0;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 ClipPos : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;

    //output.Pos.x = (input.Position.x * pushConsts.Size.x + pushConsts.Pos.x) * pushConsts.PixelSize.x;
    //output.Pos.y = (input.Position.y * pushConsts.Size.y + pushConsts.Pos.y) * pushConsts.PixelSize.y;

    //output.Pos.xy = output.Pos.xy * 2.0 - 1.0;                           // [0, 1] -> [-1, -1]
    //output.Pos.z = pushConsts.Depth;
    //output.Pos.w = 1.0;

    //output.ClipPos = output.Pos;
    //return output;

    output.Pos = mul(pushConsts.MVP, float4(input.Position, 1.0));
    output.ClipPos = output.Pos;
    return output;
}
