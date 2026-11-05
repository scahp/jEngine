#include "common.hlsl"
// Material shader injection point.
#include "Features/materialsurface.hlsl"

#ifndef USE_VERTEX_COLOR
#define USE_VERTEX_COLOR 0
#endif

#ifndef USE_FLIP_NORMALMAP_Y
#define USE_FLIP_NORMALMAP_Y 1      // todo : apply it at loading time of the normal map
#endif

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 PrevPos : POSITION0;
    float3 LocalPos : POSITION1;
    float4 TestPos : POSITION2;
#if USE_VERTEX_COLOR
    float4 Color : COLOR0;
#endif
    float2 TexCoord : TEXCOORD0;
    float3 Normal : NORMAL0;
    float4 WorldPos : TEXCOORD1;
    float3x3 TBN : TEXCOORD2;
};

struct PushConsts
{
    float4 Color;
    bool ShowVRSArea;
    bool ShowGrid;
};
[[vk::push_constant]] PushConsts pushConsts;

struct FSOutput
{
    float3 GBuffer0 : SV_TARGET0;       // World normal, encoded as RGB*0.5+0.5.
    float3 GBuffer1 : SV_TARGET1;       // Albedo.xyz
    float4 GBuffer2 : SV_TARGET2;       // Velocity.xy, Metallic, Roughness
    float3 GBuffer3 : SV_TARGET3;       // Linear emissive radiance, added once after lighting.
    float2 GBuffer4 : SV_TARGET4;       // Primary-sun direct visibility, ambient AO.
};

FSOutput main(VSOutput input
#if USE_VARIABLE_SHADING_RATE
    , uint shadingRate : SV_ShadingRate
#endif
)
{
    float4 color = 1;
#if USE_VERTEX_COLOR
    color *= input.Color;
#endif

#if USE_ALBEDO_TEXTURE
    color *= DiffuseTexture.Sample(DiffuseTextureSampler, input.TexCoord.xy);
#if !JE_HAS_MATERIAL_GRAPH
    if (RenderObjectParam.SurfaceFlags == 0 && color.w < 0.5f)
    {
        discard;
    }
#endif

    #if USE_SRGB_ALBEDO_TEXTURE
    // Convert to linear space
    color.rgb = pow(color.rgb, 2.2);
    #endif // USE_SRGB_ALBEDO_TEXTURE

    float3 normal = NormalTexture.Sample(NormalTextureSampler, input.TexCoord.xy).xyz;
    #if USE_FLIP_NORMALMAP_Y
    normal.y = 1.0 - normal.y;
    #endif // USE_FLIP_NORMALMAP_Y
    normal = normal * 2.0f - 1.0f;
    float3 WorldNormal = normalize(mul(input.TBN, normal));

#else // USE_ALBEDO_TEXTURE
    float3 WorldNormal = normalize(input.Normal);
#endif // USE_ALBEDO_TEXTURE

    color *= RenderObjectParam.BaseColorOpacity;
    float2 MaterialVisibility = float2(1.0, 1.0);
#if JE_HAS_MATERIAL_GRAPH
    JEMaterialContext MappingContext;
    MappingContext.UV = input.TexCoord;
    MappingContext.WorldPos = input.WorldPos.xyz;
    MappingContext.NormalWS = normalize(input.Normal);
    MappingContext.TBN = input.TBN;
    MappingContext.ViewDirWS = ViewParam.MaterialViewDirection.w < 0.5 ?
        normalize(ViewParam.MaterialViewDirection.xyz) : normalize(ViewParam.EyeWorld - input.WorldPos.xyz);
    const JEMaterialSurface MappedSurface = JE_ResolveMaterial(MappingContext, USE_SRGB_ALBEDO_TEXTURE != 0);
    color = MappedSurface.BaseColor;
    WorldNormal = normalize(mul(input.TBN, MappedSurface.NormalTS));
#if JE_HAS_HORIZON
    const float2 horizon = saturate(JE_EvaluateHorizon(MappingContext, ViewParam.MaterialLightDirection.xyz));
    MaterialVisibility.y = horizon.y;
    if (ViewParam.MaterialLightDirection.w > 0.5) MaterialVisibility.x = horizon.x;
#endif
#endif
    // Deferred surfaces use deterministic coverage for fractional opacity.
    // This keeps depth, shadows and the opaque GBuffer coherent without pretending
    // that the deferred pass provides sorted transparent blending.
    const uint2 pixel = uint2(input.Pos.xy);
    const float coverage = frac(dot(float2(pixel), float2(0.754877666, 0.569840296)));
    const uint surfaceFlags = RenderObjectParam.SurfaceFlags;
    if ((surfaceFlags & 8u) != 0u)
    {
        if (color.a < RenderObjectParam.SurfaceAlphaCutoff) discard;
    }
    else if (surfaceFlags == 0 || (surfaceFlags & 16u) != 0u)
    {
        if (color.a < coverage || color.a <= 0.0) discard;
    }

#if USE_VARIABLE_SHADING_RATE
    if (pushConsts.ShowVRSArea)
    {
        const uint SHADING_RATE_PER_PIXEL = 0x0;
        const uint SHADING_RATE_PER_2X1_PIXELS = 6;
        const uint SHADING_RATE_PER_1X2_PIXELS = 7;
        const uint SHADING_RATE_PER_2X2_PIXELS = 8;
        const uint SHADING_RATE_PER_4X2_PIXELS = 9;
        const uint SHADING_RATE_PER_2X4_PIXELS = 10;

        switch (shadingRate)
        {
            case SHADING_RATE_PER_PIXEL:
                color *= float4(0.0, 0.8, 0.4, 1.0);
            case SHADING_RATE_PER_2X1_PIXELS:
                color *= float4(0.2, 0.6, 1.0, 1.0);
            case SHADING_RATE_PER_1X2_PIXELS:
                color *= float4(0.0, 0.4, 0.8, 1.0);
            case SHADING_RATE_PER_2X2_PIXELS:
                color *= float4(1.0, 1.0, 0.2, 1.0);
            case SHADING_RATE_PER_4X2_PIXELS:
                color *= float4(0.8, 0.8, 0.0, 1.0);
            case SHADING_RATE_PER_2X4_PIXELS:
                color *= float4(1.0, 0.4, 0.2, 1.0);
            default:
                color *= float4(0.8, 0.0, 0.0, 1.0);
        }
    }
#endif

    //if (pushConsts.ShowGrid)
    //{
    //    float2 center = (input.DirectionalLightShadowPosition.xy * 0.5 + 0.5);

    //    float resolution = 100.0;
    //    float cellSpace = 1.0;
    //    float2 cells = abs(frac(center * resolution / cellSpace) - 0.5);

    //    float distToEdge = (0.5 - max(cells.x, cells.y)) * cellSpace;

    //    float lineWidth = 0.1;
    //    float lines = smoothstep(0.0, lineWidth, distToEdge);
    //    color = lerp(float4(0.0, 1.0, 0.0, 1.0), color, lines);
    //}

    FSOutput output = (FSOutput)0;

    float Metallic = 0;
    float Roughness = 0;
    
#if USE_PBR
#if USE_ALBEDO_TEXTURE
    float roughness = RMTexture.Sample(RMTextureSampler, input.TexCoord.xy).y;
    float metallic = RMTexture.Sample(RMTextureSampler, input.TexCoord.xy).z;
    Metallic = metallic;
    Roughness = roughness;
#else
    Metallic = RenderObjectParam.Metallic;
    Roughness = RenderObjectParam.Roughness;
#endif
#endif
    if (RenderObjectParam.EmissiveOverride.w > 0.5)
    {
        Metallic = RenderObjectParam.Metallic;
        Roughness = RenderObjectParam.Roughness;
    }
#if USE_PBR && USE_ALBEDO_TEXTURE
    if ((surfaceFlags & 2u) != 0u)
    {
        const float3 packed = RMTexture.Sample(RMTextureSampler, input.TexCoord.xy).rgb;
        if ((surfaceFlags & 4u) != 0u)
        {
            Metallic = packed.r;
            Roughness = packed.g * packed.g;
        }
        else
        {
            Metallic *= packed.b;
            Roughness *= packed.g;
        }
    }
#endif
    
#if JE_HAS_MATERIAL_GRAPH
    Metallic = MappedSurface.Metallic;
    Roughness = MappedSurface.Roughness;
#endif

    //output.GBuffer0.xy = EncodeOctNormal(WorldNormal * 0.5 + 0.5);
    output.GBuffer0.xyz = WorldNormal * 0.5 + 0.5;
    output.GBuffer1.xyz = color.xyz;
    
    float2 PrevScreenPosUV = (input.PrevPos.xy / input.PrevPos.w);
    PrevScreenPosUV.y = -PrevScreenPosUV.y;
    PrevScreenPosUV = PrevScreenPosUV * float2(0.5, 0.5) + float2(0.5, 0.5);
    
    float2 ScreenPosUV = (input.TestPos.xy / input.TestPos.w);
    ScreenPosUV.y = -ScreenPosUV.y;
    ScreenPosUV = ScreenPosUV * float2(0.5, 0.5) + float2(0.5, 0.5);
    
    //float2 ScreenPosUV = (input.Pos.xy / ViewParam.ScreenRect.zw);
    // float2 Velocity = float2(ScreenPosUV - PrevScreenPosUV) * float2(0.5, 0.5) + float2(0.5, 0.5);
    float2 Velocity = float2(ScreenPosUV - PrevScreenPosUV);
    
    // Velcoity.xy : [0, 1]
    // Metallic, Roughness : [0, 1]
    output.GBuffer2 = float4(Velocity, Metallic, Roughness);
    output.GBuffer3 = max(RenderObjectParam.EmissiveOverride.xyz, 0.0);
    output.GBuffer4 = MaterialVisibility;

    return output;
}
