// The target starts at visibility one. Stencil NOT_EQUAL zero selects shadowed
// pixels and this triangle writes visibility zero, preserving lit pixels.
struct VolumeMaskVertex
{
    float4 Pos : SV_POSITION;
};

VolumeMaskVertex VSMain(uint vertexID : SV_VertexID)
{
    const float2 uv = float2((vertexID << 1) & 2u, vertexID & 2u);
    VolumeMaskVertex output;
    output.Pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}

float4 PSMain(VolumeMaskVertex input) : SV_TARGET
{
    return float4(0.0, 0.0, 0.0, 1.0);
}
