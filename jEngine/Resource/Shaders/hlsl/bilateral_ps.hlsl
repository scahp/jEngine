#include "common.hlsl"

struct VS_OUT
{
    float4 Pos : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VS_OUT VS(uint VertexID : SV_VertexID)
{
    VS_OUT Out;
    Out.UV = float2((VertexID << 1) & 2, VertexID & 2);
    Out.Pos = float4(Out.UV * 2.0f - 1.0f, 0.0f, 1.0f);
    Out.Pos.y = -Out.Pos.y;
    return Out;
}

float4 PS(VS_OUT In) : SV_Target
{
    int2 TexelCoord = int2(In.UV * float2(Param.Width, Param.Height));
    float CenterDepth = DepthTexture.Sample(DepthTextureSampler, In.UV).r;

    if (CenterDepth >= 1.0) // Sky
    {
        return InTexture.Sample(InTextureSampler, In.UV);
    }

    float4 FinalColor = float4(0.0, 0.0, 0.0, 0.0);
    float TotalWeight = 0.0;

    int Radius = Param.KernelSize / 2;

    for (int i = -Radius; i <= Radius; i++)
    {
        for (int j = -Radius; j <= Radius; j++)
        {
            int2 Offset = int2(i, j);
            int2 SampleCoord = TexelCoord + Offset;

            // Clamp to edge
            SampleCoord = clamp(SampleCoord, int2(0, 0), int2(Param.Width - 1, Param.Height - 1));
            
            float2 SampleUV = (SampleCoord + 0.5) / float2(Param.Width, Param.Height);

            float NeighborDepth = DepthTexture.Sample(DepthTextureSampler, SampleUV).r;
            float4 NeighborColor = InTexture.Sample(InTextureSampler, SampleUV);

            float DepthDiff = abs(CenterDepth - NeighborDepth);
            float DepthWeight = exp(-(DepthDiff * DepthDiff) / (2.0 * Param.SigmaForBilateral * Param.SigmaForBilateral));

            float DistSq = dot(Offset, Offset);
            float SpatialWeight = exp(-DistSq / (2.0 * Param.Sigma * Param.Sigma));

            float Weight = DepthWeight * SpatialWeight;

            FinalColor += NeighborColor * Weight;
            TotalWeight += Weight;
        }
    }

    if (TotalWeight > 0.0)
    {
        FinalColor /= TotalWeight;
    }
    else
    {
        FinalColor = InTexture.Sample(InTextureSampler, In.UV);
    }

    return FinalColor;
}
