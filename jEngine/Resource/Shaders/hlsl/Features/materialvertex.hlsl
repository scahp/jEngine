#ifndef JENGINE_MATERIAL_VERTEX
#define JENGINE_MATERIAL_VERTEX 1

// Same explicit sum of Gerstner waves as jWaterSimulation. Four waves is the
// raster surface budget. Shadow and picking vertices use this exact function.
void JE_DeformWater(inout float3 position, inout float3 normal, inout float3 tangent)
{
    if (MaterialGraphParam.WaterControl.x < 0.5) return;
    const float2 source = position.xz;
    float3 displacement = float3(0, MaterialGraphParam.WaterControl.y, 0);
    float3 dx = float3(1, 0, 0), dz = float3(0, 0, 1);
    const uint waveCount = min((uint)MaterialGraphParam.WaterControl.w, 4u);
    [unroll] for (uint i = 0; i < 4; ++i)
    {
        if (i >= waveCount) break;
        const float4 a = MaterialGraphParam.WaterWavesA[i];
        const float4 b = MaterialGraphParam.WaterWavesB[i];
        const float k = 6.283185307179586 / max(a.w, 0.0001);
        const float phase = k * (dot(a.xy, source) - b.x * MaterialGraphParam.WaterControl.z) + b.z;
        float s, c;
        sincos(phase, s, c);
        const float qa = b.y * a.z;
        displacement += float3(qa * a.x * c, a.z * s, qa * a.y * c);
        dx += float3(-qa * k * a.x * a.x * s, a.z * k * a.x * c,
            -qa * k * a.x * a.y * s);
        dz += float3(-qa * k * a.x * a.y * s, a.z * k * a.y * c,
            -qa * k * a.y * a.y * s);
    }
    position += displacement;
    // Water is a parameterized XZ height field, matching the CPU query API.
    position.y = displacement.y;
    normal = normalize(cross(dz, dx));
    tangent = normalize(dx);
}
#endif
