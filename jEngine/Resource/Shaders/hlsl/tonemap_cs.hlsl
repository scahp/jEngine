Texture2D InputImage : register(t0);
Texture2D EyeAdaptationImage : register(t1);
RWTexture2D<float4> ResultImage : register(u2);

struct CommonComputeUniformBuffer
{
    float Width;
    float Height;
    float UseWaveIntrinsics;
    float Padding;
};

cbuffer ComputeCommon : register(b3)
{
    CommonComputeUniformBuffer ComputeCommon;
}

// http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 Uncharted2Tonemap(float3 x)
{
    float A = 0.15;		// Shoulder Strength
    float B = 0.50;		// Linear Strength
    float C = 0.10;		// Linear Angle
    float D = 0.20;		// Toe Strength
    float E = 0.02;		// Toe Numerator
    float F = 0.30;		// Toe Denominator
    float W = 11.2;		// Linear White Point Value

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
float3 ACESFilmTonemap(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// http://filmicworlds.com/blog/filmic-tonemapping-with-piecewise-power-curves/		// wait for implementation

// Applies the filmic curve from John Hable's presentation
float3 FilmicToneMapALU(float3 linearColor)
{
    float3 color = max(0.0, linearColor - 0.004);
    color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);

    // result has 1/2.2 baked in
    return pow(color, 2.2);
}

float GetExposureScale()
{
    return EyeAdaptationImage.Load(int3(0, 0, 0)).x;
}

[numthreads(16, 16, 1)]
void main(uint3 GlobalInvocationID : SV_DispatchThreadID)
{   
    if (GlobalInvocationID.x >= ComputeCommon.Width || GlobalInvocationID.y >= ComputeCommon.Height)
        return;
    
    float4 color = InputImage[uint2(GlobalInvocationID.xy)];
    color.xyz *= GetExposureScale();
    color.xyz = Uncharted2Tonemap(color.xyz);

    color.xyz = pow(color.xyz, 1.0 / 2.2);
    ResultImage[int2(GlobalInvocationID.xy)] = color;
}
