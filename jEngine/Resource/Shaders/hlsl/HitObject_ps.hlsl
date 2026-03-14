#include "common.hlsl"

struct PSInput
{
    float4 Pos : SV_POSITION;
};

struct PSOutput
{
    float4 RenderObjectID : SV_Target0;
};

PSOutput main(PSInput input)
{
    PSOutput output = (PSOutput) 0;

    // Encode RenderObjectID as RGBA (uint32 -> 4 bytes)
    uint renderObjectID = RenderObjectParam.RenderObjectID;

    output.RenderObjectID = float4(
        float((renderObjectID >> 0) & 0xFF) / 255.0,
        float((renderObjectID >> 8) & 0xFF) / 255.0,
        float((renderObjectID >> 16) & 0xFF) / 255.0,
        float((renderObjectID >> 24) & 0xFF) / 255.0
    );

    return output;
}
