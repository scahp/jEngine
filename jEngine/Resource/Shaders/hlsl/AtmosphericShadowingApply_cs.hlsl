
struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    int Paading0;
    int Padding1;
};

RWTexture2D<float4> ResultImage : register(u0);
Texture2D Texture : register(t1);
SamplerState TextureSampler : register(s1);
cbuffer ComputeCommon : register(b2)
{
    CommonComputeUniformBuffer ComputeCommon;
}

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    float2 uv = GlobalInvocationID.xy / float2(ComputeCommon.Width - 1, ComputeCommon.Height - 1);
    
    float t = Texture.SampleLevel(TextureSampler, uv, 0).x;
    ResultImage[int2(GlobalInvocationID.xy)] += float4(t, t, t, 1);
}
