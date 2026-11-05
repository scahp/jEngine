// Skin views keep the geometric surface normal distinct from fine pore detail.
// Both octahedral pairs fit one RGBA16F MRT. Post passes receive the historical
// RGB geometric-normal encoding after the separate post-lighting decode.
float2 JE_SkinOctEncode(float3 value)
{
    value /= max(abs(value.x) + abs(value.y) + abs(value.z), 1.0e-8);
    if (value.z < 0.0)
        value.xy = (1.0 - abs(value.yx)) * float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
    return value.xy * 0.5 + 0.5;
}
float3 JE_SkinOctDecode(float2 value)
{
    value = value * 2.0 - 1.0;
    float3 normal = float3(value, 1.0 - abs(value.x) - abs(value.y));
    const float t = saturate(-normal.z);
    normal.xy += float2(normal.x >= 0.0 ? -t : t, normal.y >= 0.0 ? -t : t);
    return normalize(normal);
}
