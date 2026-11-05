#include "common.hlsl"
#include "Features/materialmapping.hlsl"
// Material shader injection point.
#include "Features/materialsurface.hlsl"
#include "Features/skinnormalpacking.hlsl"
#include "Features/skinshading.hlsl"
#include "Features/texturesubsurface.hlsl"
#ifndef JE_TEXTURE_SSS_CAPTURE
#define JE_TEXTURE_SSS_CAPTURE 0
#endif
#ifndef USE_SRGB_ALBEDO_TEXTURE
#define USE_SRGB_ALBEDO_TEXTURE 0
#endif
#ifndef USE_ALBEDO_TEXTURE
#define USE_ALBEDO_TEXTURE 0
#endif
struct TextureSSSVarying
{
    float4 Position : SV_POSITION;
    float3 World : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float3 Normal : TEXCOORD2;
    float3x3 TBN : TEXCOORD3;
    float4 Color : COLOR0;
};
JEMaterialSurface JE_TSSSMaterial(TextureSSSVarying input)
{
    JEMaterialContext ctx;
    ctx.UV=input.UV; ctx.WorldPos=input.World; ctx.NormalWS=normalize(input.Normal);
    ctx.TBN=input.TBN; ctx.ViewDirWS=normalize(TextureSSS.EyeWorld.xyz-input.World);
#if JE_HAS_MATERIAL_GRAPH
    return JE_ResolveMaterial(ctx,USE_SRGB_ALBEDO_TEXTURE!=0);
#else
    JEMaterialSurface value=(JEMaterialSurface)0;
    value.BaseColor=RenderObjectParam.BaseColorOpacity*input.Color;
    value.NormalTS=float3(0,0,1);
#if USE_ALBEDO_TEXTURE
    float4 texel=DiffuseTexture.Sample(DiffuseTextureSampler,input.UV);
#if USE_SRGB_ALBEDO_TEXTURE
    texel.rgb=pow(max(texel.rgb,0.0),2.2);
#endif
    value.BaseColor*=texel;
    float3 mapped=NormalTexture.Sample(NormalTextureSampler,input.UV).rgb;
    mapped.y=1.0-mapped.y;
    value.NormalTS=normalize(mapped*2.0-1.0);
#endif
    value.Roughness=RenderObjectParam.Roughness;
    value.Metallic=RenderObjectParam.Metallic;
    value.SubsurfaceWeight=MaterialGraphParam.MaterialSubsurfaceColorWeight.a;
    value.SubsurfaceColor=MaterialGraphParam.MaterialSubsurfaceColorWeight.rgb;
    value.SubsurfaceThickness=MaterialGraphParam.MaterialSubsurfaceParameters.x;
    value.SubsurfaceProfile=(uint)clamp(MaterialGraphParam.MaterialSubsurfaceParameters.y,0.0,7.0);
    value.ShadingFlags=(uint)MaterialGraphParam.MaterialSkinParameters.x;
    value.SpecularWeight=MaterialGraphParam.MaterialSkinParameters.y;
    value.DiffuseOcclusion=MaterialGraphParam.MaterialSkinParameters.z;
    value.PreScatterWeight=MaterialGraphParam.MaterialSkinParameters.w;
    return value;
#endif
}
#if JE_TEXTURE_SSS_CAPTURE
struct CaptureOutput
{
    float4 Entry : SV_TARGET0;
    float4 IdentityNormal : SV_TARGET1;
};
CaptureOutput main(TextureSSSVarying input)
{
    CaptureOutput output=(CaptureOutput)0;
    const JEMaterialSurface material=JE_TSSSMaterial(input);
    const float3 normal=normalize(mul(input.TBN,material.NormalTS));
    const float3 delta=input.World-TextureSSS.KeyPositionAndReference.xyz;
    const float distance=TextureSSS.KeyColorAndType.w>.5?length(delta):dot(delta,TextureSSS.DirectionAndFar.xyz);
    if (!all(isfinite(float4(normal,distance))) || distance<=0.0) discard;
    const bool selected=TextureSSS.Draw.w>.5 && (material.ShadingFlags&255u)==1u &&
        ((material.ShadingFlags>>8u)&255u)==1u && material.SubsurfaceThickness>0.0;
    output.Entry=float4(distance,input.UV,selected?min(material.SubsurfaceProfile,7u)+1u:0u);
    // A depth sample lies on the actual raster triangle. Its plane normal
    // permits receiver-ray correction without treating smooth/pore normals as
    // a geometric depth slope. Align orientation for the transmission term.
    float3 geometricNormal=cross(ddx(input.World),ddy(input.World));
    const float normalLength2=dot(geometricNormal,geometricNormal);
    geometricNormal=normalLength2>1.e-12?geometricNormal*rsqrt(normalLength2):normalize(input.Normal);
    if(dot(geometricNormal,input.Normal)<0.0) geometricNormal=-geometricNormal;
    output.IdentityNormal=float4(TextureSSS.Draw.yz,JE_SkinOctEncode(geometricNormal));
    return output;
}
#else
struct BakeOutput
{
    float4 Irradiance : SV_TARGET0;
    float4 Stretch : SV_TARGET1;
    float4 EntryUV : SV_TARGET2;
    float4 Finish : SV_TARGET3;
};
BakeOutput main(TextureSSSVarying input)
{
    BakeOutput output=(BakeOutput)0;
    const JEMaterialSurface material=JE_TSSSMaterial(input);
    if ((material.ShadingFlags&255u)!=1u || ((material.ShadingFlags>>8u)&255u)!=1u ||
        material.SubsurfaceThickness<=0.0 || !all(isfinite(input.World)) ||
        any(input.UV<0.0) || any(input.UV>1.0)) discard;
    const uint profile=min(material.SubsurfaceProfile,7u);
    // This module's measured six-lobe coefficients implement Skin (ID0).
    // Other stable material profiles retain the independent screen result.
    if (profile!=0u) discard;
    const float3 N=normalize(mul(input.TBN,material.NormalTS));
    const float3 toLight=TextureSSS.KeyPositionAndReference.xyz-input.World;
    const float distanceSquared=max(dot(toLight,toLight),1.0e-6);
    const float3 L=TextureSSS.KeyColorAndType.w>.5?toLight*rsqrt(distanceSquared):-TextureSSS.DirectionAndFar.xyz;
    const float noL=dot(N,L);
    const float geometricNoL=dot(normalize(input.Normal),L);
    float3 radiance=max(TextureSSS.KeyColorAndType.rgb,0.0);
    if (TextureSSS.KeyColorAndType.w>.5)
        radiance*=TextureSSS.KeyPositionAndReference.w*TextureSSS.KeyPositionAndReference.w/distanceSquared *
            JE_TSSSRange(distanceSquared,TextureSSS.Features.w);
    float2 lightUV;
    const bool captured=JE_TSSSCaptureUV(mul(TextureSSS.LightVP,float4(input.World,1.0)),lightUV);
    float4 entry=0.0, identity=0.0;
    float2 entryPixelUV=0.0;
    if (captured)
    {
        const int2 q=clamp((int2)(lightUV*TextureSSS.CaptureSizeAndInvSize.xy),0,(int2)TextureSSS.CaptureSizeAndInvSize.xy-1);
        entry=TextureSSSCapture0.Load(int3(q,0));
        identity=TextureSSSCapture1.Load(int3(q,0));
        entryPixelUV=(float2(q)+.5)*TextureSSS.CaptureSizeAndInvSize.zw;
    }
    const float receiverDistance=TextureSSS.KeyColorAndType.w>.5?sqrt(distanceSquared):
        dot(input.World-TextureSSS.KeyPositionAndReference.xyz,TextureSSS.DirectionAndFar.xyz);
    bool entryValid=captured && entry.x>0.0 && all(isfinite(entry)) && all(isfinite(identity));
    const float3 Ni=JE_SkinOctDecode(identity.zw);
    float planeDistance=entry.x;
    if(entryValid)
    {
        const float4 projected=mul(TextureSSS.InvLightVP,
            float4(entryPixelUV*float2(2.0,-2.0)+float2(-1.0,1.0),.5,1.0));
        entryValid=all(isfinite(projected)) && abs(projected.w)>1.e-8;
        if(entryValid)
        {
            const float3 sampleWorld=projected.xyz/projected.w;
            const float3 key=TextureSSS.KeyPositionAndReference.xyz;
            const bool finiteKey=TextureSSS.KeyColorAndType.w>.5;
            const float3 ray=finiteKey?-L:TextureSSS.DirectionAndFar.xyz;
            const float3 entryPoint=finiteKey?key+normalize(sampleWorld-key)*entry.x:
                sampleWorld+ray*(entry.x-dot(sampleWorld-key,ray));
            const float3 origin=finiteKey?key:input.World-ray*receiverDistance;
            entryValid=JE_TSSSReceiverPlaneDistance(origin,ray,entryPoint,Ni,
                TextureSSS.DirectionAndFar.w,planeDistance);
        }
    }
    const float thickness=max(receiverDistance-planeDistance,0.0);
    // Explicit out-of-capture rejection; a missing capture cannot invent
    // transmitted light or an unshadowed key outside the fitted light frustum.
    const float lit=entryValid && receiverDistance<=planeDistance+TextureSSS.Physical.w?1.0:0.0;
    const float energy=JE_SkinEnergyLookup(TextureSSSEnergyLUT,DiffuseTextureSampler,saturate(noL),material.Roughness);
    const float diffuseEnergy=JE_SkinDiffuseEnergy(material.SpecularWeight,energy);
    const float3 albedo=max(material.BaseColor.rgb,0.0);
    const float pre=saturate(material.PreScatterWeight);
    output.Irradiance.rgb=radiance*(saturate(noL)*lit*diffuseEnergy*saturate(material.DiffuseOcclusion))*pow(albedo,pre);
    const bool sameSurface=entryValid && JE_TSSSID(identity.xy)==JE_TSSSID(TextureSSS.Draw.yz) &&
        abs(entry.w-(profile+1.0))<.25;
    // Use the actual sampled front-entry normal Ni, not the exit normal N.
    const float corrected=JE_TSSSThicknessMm(thickness,TextureSSS.Physical.x,geometricNoL,dot(Ni,normalize(input.Normal)));
    const bool transmission=TextureSSS.Features.x>.5 && sameSurface && geometricNoL<-.01 &&
        thickness>TextureSSS.Physical.w && corrected>0.0;
    output.Irradiance.a=transmission?exp(-min(80.0,TextureSSS.Physical.y*corrected*TextureSSS.Physical.z)):0.0;
    // Additive coverage makes real UV overlaps observable. Four-load support
    // tests reject those footprints locally, preserving the screen result.
    output.Stretch=float4(max(length(ddx(input.World)),1.0e-5),max(length(ddy(input.World)),1.0e-5),1.0,profile+1.0);
    output.EntryUV=float4(entry.yz,thickness,!captured?-1.0:(!entryValid?-2.0:(transmission?profile+1.0:0.0)));
    output.Finish=float4(pow(albedo,1.0-pre),material.SubsurfaceThickness);
    return output;
}
#endif
