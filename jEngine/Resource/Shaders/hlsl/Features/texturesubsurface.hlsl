// Resource/declaration-free transport helpers. Distances are explicit world
// units or millimetres; texture-space variance is never a screen pixel radius.
uint JE_TSSSID(float2 packed)
{
    const uint2 bits = (uint2)round(packed);
    return bits.x <= 1023u && bits.y <= 1023u ? bits.x | (bits.y << 10u) : 0u;
}
float JE_TSSSRange(float distanceSquared, float maximumDistance)
{
    if (maximumDistance <= 0.0) return 1.0;
    const float x = distanceSquared / (maximumDistance * maximumDistance);
    const float window = saturate(1.0 - x*x);
    return window*window;
}
bool JE_TSSSCaptureUV(float4 projected, out float2 uv)
{
    uv = 0.0;
    if (!all(isfinite(projected)) || projected.w <= 1.0e-7) return false;
    const float3 ndc = projected.xyz / projected.w;
    uv = ndc.xy * float2(0.5,-0.5) + 0.5;
    return all(uv >= 0.0) && all(uv <= 1.0) && ndc.z >= 0.0 && ndc.z <= 1.0;
}
float JE_TSSSThicknessMm(float thicknessWorld, float unitsPerMm, float exitNoL, float entryExitDot)
{
    const float backFacing = saturate(-entryExitDot);
    return max(thicknessWorld,0.0)/max(unitsPerMm,1.0e-5) * lerp(1.0,saturate(-exitNoL),backFacing);
}
bool JE_TSSSReceiverPlaneDistance(float3 origin, float3 ray, float3 entryPoint,
    float3 entryNormal, float maximumDistance, out float distance)
{
    distance = 0.0;
    const float denominator = dot(entryNormal, ray);
    if (!all(isfinite(float4(entryNormal, maximumDistance))) ||
        !all(isfinite(origin)) || !all(isfinite(ray)) || !all(isfinite(entryPoint)) ||
        abs(denominator) < 1.0e-4 || maximumDistance <= 0.0) return false;
    const float candidate = dot(entryNormal, entryPoint - origin) / denominator;
    if (!isfinite(candidate) || candidate <= 0.0 || candidate > maximumDistance) return false;
    distance = candidate;
    return true;
}
float4 JE_TSSSBilinear(Texture2D<float4> image, float2 uv, uint2 origin, uint tile)
{
    const float2 p = saturate(uv)*tile - 0.5;
    const int2 lo = (int2)floor(p);
    const float2 f = frac(p);
    const int2 maximum = int(tile)-1;
    const float4 a = image.Load(int3(origin + (uint2)clamp(lo,0,maximum),0));
    const float4 b = image.Load(int3(origin + (uint2)clamp(lo+int2(1,0),0,maximum),0));
    const float4 c = image.Load(int3(origin + (uint2)clamp(lo+int2(0,1),0,maximum),0));
    const float4 d = image.Load(int3(origin + (uint2)clamp(lo+int2(1,1),0,maximum),0));
    return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
// UV maps need not be globally one-to-one (the reference eyes overlap).
// Reject every bilinear footprint that can touch overlap, absent chart or a
// different profile. A tiny interpolation weight cannot hide invalid coverage.
bool JE_TSSSSingleFootprint(Texture2D<float4> stretch,Texture2D<float4> entry,float2 uv,uint2 origin,uint tile)
{
    if(!all(isfinite(uv)) || any(uv<0.0) || any(uv>1.0)) return false;
    const int2 first=(int2)floor(uv*tile-.5);
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x)
    {
        const int2 p=clamp(first+int2(x,y),0,int(tile)-1);
        const float4 value=stretch.Load(int3(origin+(uint2)p,0));
        if(!all(isfinite(value)) || value.z<.99 || value.z>1.01 || abs(value.w-1.0)>.25) return false;
        if(entry.Load(int3(origin+(uint2)p,0)).a<0.0) return false;
    }
    return true;
}
uint2 JE_TSSSTileOrigin(uint index, uint columns, uint tile)
{ return uint2(index%columns,index/columns)*tile; }
