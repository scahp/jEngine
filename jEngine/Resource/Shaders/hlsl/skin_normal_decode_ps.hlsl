#include "Features/skinnormalpacking.hlsl"

float4 main(float4 position : SV_POSITION) : SV_TARGET0
{
    return float4(JE_SkinOctDecode(PackedNormal.Load(int3(int2(position.xy), 0)).xy) * 0.5 + 0.5, 1.0);
}
