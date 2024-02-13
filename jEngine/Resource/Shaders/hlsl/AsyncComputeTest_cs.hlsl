
struct CommonComputeUniformBuffer
{
    int Width;
    int Height;
    int Paading0;
    int Padding1;
};

RWTexture2D<float4> ResultImage : register(u0);

cbuffer ComputeCommon : register(b1)
{
    CommonComputeUniformBuffer ComputeCommon;
}

[numthreads(16, 16, 1)]
void AsyncComputeTest_A(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    float t = 0.0f;
    for (int i = 0; i < 1500; ++i)
    {
        t += ResultImage[int2(GlobalInvocationID.xy)].x * 0.0001f;
    }
    
    ResultImage[int2(GlobalInvocationID.xy)] = float4(t, 0, 0, 1);
}

[numthreads(16, 16, 1)]
void AsyncComputeTest_B(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    float t = 0.0f;
    for (int i = 0; i < 1500; ++i)
    {
        t += ResultImage[int2(GlobalInvocationID.xy)].x * 0.0001f;
    }
    
    float x = ResultImage[int2(GlobalInvocationID.xy)].x;
    
    ResultImage[int2(GlobalInvocationID.xy)] = float4(x, x + t, 0, 1);
}

[numthreads(16, 16, 1)]
void AsyncComputeTest_C(uint3 GlobalInvocationID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    float t = 0.0f;
    for (int i = 0; i < 1500; ++i)
    {
        t += ResultImage[int2(GlobalInvocationID.xy)].x * 0.0001f;
    }
    
    float2 xy = ResultImage[int2(GlobalInvocationID.xy)].xy;
    
    ResultImage[int2(GlobalInvocationID.xy)] = float4(xy.x, xy.y, (xy.y + t) * 0.5, 1);
}
