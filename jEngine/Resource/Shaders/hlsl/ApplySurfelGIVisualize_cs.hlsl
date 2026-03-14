#include "common.hlsl"

[numthreads(8, 8, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{
    const int2 pixel = int2(GlobalInvocationID.xy);
    if (pixel.x >= ApplyCommon.SceneWidth || pixel.y >= ApplyCommon.SceneHeight)
        return;

    const float3 sceneColor = SceneColorInput.Load(int3(pixel, 0)).xyz;
    const float4 visualizeSample = SurfelVisualizeInput.Load(int3(pixel, 0));
    const float3 visualizeColor = visualizeSample.xyz;
    const float surfelMask = step(0.5, visualizeSample.w);
    const float blend = surfelMask * saturate(ApplyCommon.BlendAlpha);
    OutSceneColor[pixel] = float4(lerp(sceneColor, visualizeColor, blend), 1.0);
}
