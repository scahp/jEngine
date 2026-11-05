#ifndef JENGINE_MATERIAL_MAPPING_TYPES
#define JENGINE_MATERIAL_MAPPING_TYPES 1

struct JEMaterialContext
{
    float2 UV;
    float3 WorldPos;
    float3 NormalWS;
    float3x3 TBN;
    float3 ViewDirWS;
};

struct JEMaterialSurface
{
    float4 BaseColor;
    float3 NormalTS;
    float Metallic;
    float Roughness;
    float SubsurfaceWeight;
    float3 SubsurfaceColor;
    float SubsurfaceThickness;
    uint SubsurfaceProfile;
    float SpecularWeight;
    float DiffuseOcclusion;
    float PreScatterWeight;
    uint ShadingFlags;
};

#endif
