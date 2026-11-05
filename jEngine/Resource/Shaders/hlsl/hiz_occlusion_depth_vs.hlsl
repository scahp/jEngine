#include "common.hlsl"
// Hi-Z depth shader injection point; declarations precede shader bodies.
struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION0;
};
float4 main(VSInput input) : SV_POSITION
{
    // Same operation order as the base vertex shader. Only validated opaque,
    // undeformed meshes enter this pass; bounds or boxes never write depth.
    return mul(HiZDepth.ViewProjection, mul(HiZDepth.World, float4(input.Position, 1.0)));
}
