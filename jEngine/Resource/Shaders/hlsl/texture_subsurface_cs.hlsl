#include "common.hlsl"
#include "Features/texturesubsurface.hlsl"
#ifndef JE_TEXTURE_SSS_PASS
#define JE_TEXTURE_SSS_PASS 0
#endif
static const float JE_TSSSGaussian[7]={.006,.061,.242,.383,.242,.061,.006};

bool JE_TSSSAtlasPixel(uint2 pixel,out uint index,out uint2 origin,out float2 uv)
{
    const uint tile=(uint)TextureSSS.Atlas.z, columns=(uint)TextureSSS.Atlas.x;
    const uint2 cell=pixel/tile;
    index=cell.y*columns+cell.x;
    origin=cell*tile;
    uv=(float2(pixel-origin)+.5)/tile;
    return index<(uint)TextureSSS.Atlas.w;
}
#if JE_TEXTURE_SSS_PASS==5
float4 JE_TSSSSampleLevel(uint level,float2 uv,uint2 origin,uint tile)
{
    if (level==0u) return JE_TSSSBilinear(TextureSSSLevel0,uv,origin,tile);
    if (level==1u) return JE_TSSSBilinear(TextureSSSLevel1,uv,origin,tile);
    if (level==2u) return JE_TSSSBilinear(TextureSSSLevel2,uv,origin,tile);
    if (level==3u) return JE_TSSSBilinear(TextureSSSLevel3,uv,origin,tile);
    if (level==4u) return JE_TSSSBilinear(TextureSSSLevel4,uv,origin,tile);
    return JE_TSSSBilinear(TextureSSSLevel5,uv,origin,tile);
}
float3 JE_TSSSWeighted(float2 uv,uint2 origin,uint tile)
{
    float3 result=0.0;
    [unroll] for(uint i=0u;i<6u;++i)
        result+=TextureSSS.Lobe[i].rgb*JE_TSSSSampleLevel(i,uv,origin,tile).rgb;
    return result;
}
#endif

[numthreads(8,8,1)]
void main(uint3 thread:SV_DispatchThreadID)
{
    const uint2 pixel=thread.xy;
#if JE_TEXTURE_SSS_PASS==0
    if(pixel.y==0u && pixel.x<4u) TextureSSSSurfaceFlags[pixel]=0u;
    return;
#elif JE_TEXTURE_SSS_PASS==6
    const uint2 dimensions=(uint2)TextureSSS.OutputSizeAndInvSize.xy;
    if(any(pixel>=dimensions)) return;
    const float4 scene=TextureSSSScene.Load(int3(pixel,0));
    TextureSSSResult[pixel]=scene;
    const float4 previousDiffuse=TextureSSSScreenDiffuse.Load(int3(pixel,0));
    TextureSSSCompletedDiffuse[pixel]=previousDiffuse;
    const float4 mask=TextureSSSMask.Load(int3(pixel,0));
    const uint flags=(uint)round(TextureSSSAlbedo.Load(int3(pixel,0)).a);
    if((flags&255u)!=1u || ((flags>>8u)&255u)!=1u ||
        abs(mask.a-1.0)>.25 || !any(mask.rgb>0.0) || TextureSSS.Features.z<=0.0) return;
    const float4 uvID=TextureSSSSkinUV.Load(int3(pixel,0));
    if(!all(isfinite(uvID)) || any(uvID.rg<0.0) || any(uvID.rg>1.0)) return;
    const uint id=JE_TSSSID(uvID.ba);
    uint surface=4u;
    [unroll] for(uint s=0u;s<4u;++s)
        if(s<(uint)TextureSSS.Atlas.w && JE_TSSSID(TextureSSS.SurfaceLookup[s].xy)==id && id!=0u) surface=s;
    if(surface>=4u || (TextureSSSSurfaceFlags[uint2(surface,0)]&6u)!=0u) return;
    const uint tile=(uint)TextureSSS.Atlas.z;
    const uint2 origin=JE_TSSSTileOrigin(surface,(uint)TextureSSS.Atlas.x,tile);
    if(!JE_TSSSSingleFootprint(TextureSSSStretch,TextureSSSEntryUV,uvID.rg,origin,tile)) return;
    // Mask.a already requires positive authored thickness; retain the same
    // coherent thickness witness without binding the UV Finish atlas here.
    if(previousDiffuse.a<=0.0) return;
    const float3 filtered=JE_TSSSBilinear(TextureSSSInput,uvID.rg,origin,tile).rgb;
    const float3 previous=previousDiffuse.rgb;
    const float3 composed=scene.rgb+saturate(mask.rgb)*TextureSSS.Features.z*(filtered-previous);
    if(all(isfinite(composed)))
    {
        TextureSSSResult[pixel]=float4(composed,scene.a);
        TextureSSSCompletedDiffuse[pixel]=float4(filtered,previousDiffuse.a);
    }
    return;
#else
    const uint tile=(uint)TextureSSS.Atlas.z;
    const uint2 dimensions=(uint2)TextureSSS.Atlas.xy*tile;
    if(any(pixel>=dimensions)) return;
    uint surface;uint2 origin;float2 uv;
    if(!JE_TSSSAtlasPixel(pixel,surface,origin,uv))
    {
#if JE_TEXTURE_SSS_PASS!=1
        TextureSSSResult[pixel]=0.0;
#endif
        return;
    }
    const float4 stretch=TextureSSSStretch.Load(int3(pixel,0));
#if JE_TEXTURE_SSS_PASS==1
    uint rejected=0u;
    if(stretch.z>1.01) rejected|=1u; // overlapping UVs are not a single-valued surface.
    const float entryStatus=TextureSSSEntryUV.Load(int3(pixel,0)).a;
    if(stretch.z>.5 && entryStatus<-.5 && entryStatus>-1.5) rejected|=2u;
    if(stretch.z>.5 && (!all(isfinite(stretch)) ||
        !all(isfinite(TextureSSSLevel0.Load(int3(pixel,0)))) ||
        !all(isfinite(TextureSSSFinish.Load(int3(pixel,0)))))) rejected|=4u;
    if(rejected!=0u) InterlockedOr(TextureSSSSurfaceFlags[uint2(surface,0)],rejected);
#elif JE_TEXTURE_SSS_PASS==3
    TextureSSSResult[pixel]=saturate(stretch.z).xxxx;
#elif JE_TEXTURE_SSS_PASS==2 || JE_TEXTURE_SSS_PASS==4
    const float4 center=TextureSSSInput.Load(int3(pixel,0));
#if JE_TEXTURE_SSS_PASS==2
    if(stretch.z<.99 || stretch.z>1.01 || abs(stretch.w-1.0)>.25 || (TextureSSSSurfaceFlags[uint2(surface,0)]&6u)!=0u)
    { TextureSSSResult[pixel]=center;return; }
#endif
    const float worldPerPixel=TextureSSS.Filter.y<.5?stretch.x:stretch.y;
    // Authored world thickness bounds the full three-sigma profile support;
    // it does not replace the measured front-to-exit TSM thickness.
    const float authoredThickness=max(TextureSSSFinish.Load(int3(pixel,0)).a,0.0);
    const float maximumSigmaWorld=2.722132*TextureSSS.Physical.x*TextureSSS.Features.y;
    const float radiusScale=min(1.0,authoredThickness/max(3.0*maximumSigmaWorld,1.0e-5));
    const float sigmaPixels=clamp(TextureSSS.Filter.x*TextureSSS.Physical.x*TextureSSS.Features.y*radiusScale/
        max(worldPerPixel,1.0e-5),0.0,64.0);
    const float2 axis=TextureSSS.Filter.y<.5?float2(1.0,0.0):float2(0.0,1.0);
    float4 sum=0.0;
    [unroll] for(int tap=0;tap<7;++tap)
    {
        const float2 tapUV=uv+axis*(float(tap-3)*sigmaPixels/tile);
        float4 value=JE_TSSSBilinear(TextureSSSInput,tapUV,origin,tile);
#if JE_TEXTURE_SSS_PASS==2
        // Return missing/other-profile tap energy to this surface's center.
        if(!JE_TSSSSingleFootprint(TextureSSSStretch,TextureSSSEntryUV,tapUV,origin,tile)) value=center;
#endif
        sum+=value*(JE_TSSSGaussian[tap]/1.001);
    }
    TextureSSSResult[pixel]=sum;
#elif JE_TEXTURE_SSS_PASS==5
    const uint rejectionFlags=TextureSSSSurfaceFlags[uint2(surface,0)];
    if(stretch.z<.99 || stretch.z>1.01 || (TextureSSSSurfaceFlags[uint2(surface,0)]&6u)!=0u)
    { TextureSSSResult[pixel]=float4(0.0,0.0,0.0,float(rejectionFlags));return; }
    float3 blurred=JE_TSSSWeighted(uv,origin,tile);
    const float seam=pow(saturate(TextureSSSCoverage.Load(int3(pixel,0)).r),3.0);
    blurred=lerp(TextureSSSLevel0.Load(int3(pixel,0)).rgb,blurred,seam);
    const float4 entry=TextureSSSEntryUV.Load(int3(pixel,0));
    if(TextureSSS.Features.x>.5 && abs(entry.a-1.0)<.25 && all(entry.rg>=0.0) && all(entry.rg<=1.0))
    {
        if(JE_TSSSSingleFootprint(TextureSSSStretch,TextureSSSEntryUV,entry.rg,origin,tile))
        {
            // Four broader decoded-alpha thickness estimates, in mm. The
            // encoding/decoding share extinction and explicit unit conversion.
            static const float a[4]={.433,.753,1.412,2.722};
            const float2 delta=(uv-entry.rg)*tile;
            const float separation=length(delta*max(stretch.xy,1.0e-5))/max(TextureSSS.Physical.x,1.0e-5);
            float3 transmitted=0.0;
            [unroll] for(uint i=0u;i<4u;++i)
            {
                const uint level=i+2u;
                const float alpha=clamp(JE_TSSSSampleLevel(level,uv,origin,tile).a,1.0e-6,1.0);
                const float thickness=-log(alpha)/max(TextureSSS.Physical.y,1.0e-5);
                const float fade=exp(-min(80.0,thickness*thickness/(2.0*a[i]*a[i])));
                const float3 donor=JE_TSSSSampleLevel(level,entry.rg,origin,tile).rgb;
                const float3 local=JE_TSSSSampleLevel(level,uv,origin,tile).rgb;
                const float blend=saturate(separation/(6.0*a[i]));
                transmitted+=TextureSSS.Lobe[level].rgb*lerp(local,donor,blend)*fade;
            }
            blurred+=transmitted;
        }
    }
    const float3 finished=max(blurred,0.0)*TextureSSSFinish.Load(int3(pixel,0)).rgb;
    TextureSSSResult[pixel]=float4(all(isfinite(finished))?finished:0.0,float(rejectionFlags));
#endif
#endif
}
