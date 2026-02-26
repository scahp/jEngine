#include "common.hlsl"

struct ApplyVisualizeUniformBuffer
{
    float BlendAlpha;
    int SceneWidth;
    int SceneHeight;
    int Padding0;
};

RWTexture2D<float4> OutSceneColor : register(u0);
Texture2D SceneColorInput : register(t1);
Texture2D SurfelVisualizeInput : register(t2);

cbuffer ApplyVisualizeCommon : register(b3)
{
    ApplyVisualizeUniformBuffer ApplyCommon;
}

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
