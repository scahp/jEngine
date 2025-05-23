// This is moidifed version of the https://github.com/knightcrawler25/GLSL-PathTracer repository.

#pragma once
#include "Math/Vector.h"
#include "RHI/jTexture.h"
#include "Scene/jCamera.h"

#define PATH_TRACING_DATA_LEFT_HAND 1

struct jRenderOptions
{
    jRenderOptions()
    {
        renderResolution = Vector2i(1280, 720);
        windowResolution = Vector2i(1280, 720);
        uniformLightCol = Vector(0.3f, 0.3f, 0.3f);
        backgroundCol = Vector(1.0f, 1.0f, 1.0f);
        tileWidth = 100;
        tileHeight = 100;
        maxDepth = 2;
        maxSpp = -1;
        RRDepth = 2;
        texArrayWidth = 2048;
        texArrayHeight = 2048;
        denoiserFrameCnt = 20;
        enableRR = true;
        enableDenoiser = false;
        enableTonemap = true;
        enableAces = false;
        openglNormalMap = true;
        enableEnvMap = false;
        enableUniformLight = false;
        hideEmitters = false;
        enableBackground = false;
        transparentBackground = false;
        independentRenderSize = false;
        enableRoughnessMollification = false;
        enableVolumeMIS = false;
        envMapIntensity = 1.0f;
        envMapRot = 0.0f;
        roughnessMollificationAmt = 0.0f;
    }

    Vector2i renderResolution;
    Vector2i windowResolution;
    Vector uniformLightCol;
    Vector backgroundCol;
    int32 tileWidth;
    int32 tileHeight;
    int32 maxDepth;
    int32 maxSpp;
    int32 RRDepth;
    int32 texArrayWidth;
    int32 texArrayHeight;
    int32 denoiserFrameCnt;
    bool enableRR;
    bool enableDenoiser;
    bool enableTonemap;
    bool enableAces;
    bool simpleAcesFit;
    bool openglNormalMap;
    bool enableEnvMap;
    bool enableUniformLight;
    bool hideEmitters;
    bool enableBackground;
    bool transparentBackground;
    bool independentRenderSize;
    bool enableRoughnessMollification;
    bool enableVolumeMIS;
    float envMapIntensity;
    float envMapRot;
    float roughnessMollificationAmt;
};

enum LightType
{
    RectLight,
    SphereLight,
    DistantLight
};

struct Light
{
    Vector position;
    Vector emission;
    Vector u;
    Vector v;
    float radius;
    float area;
    uint32 type;
};

struct Indices
{
    int32 x, y, z;
};

class Mesh
{
public:
    Mesh()
    {
    }
    ~Mesh() { }

    bool LoadFromFile(const std::string& filename);

    std::vector<Vector4> verticesUVX; // Vertex + texture Coord (u/s)
    std::vector<Vector> normalsUVY;  // Normal + texture Coord (v/t)

    std::string name;
};

class MeshInstance
{

public:
    MeshInstance(std::string name, int meshId, Matrix xform, int matId)
        : name(name)
        , meshID(meshId)
        , transform(xform)
        , materialID(matId)
    {
    }
    ~MeshInstance() {}

    Matrix transform;
    std::string name;

    int materialID;
    int meshID;
};

enum AlphaMode
{
    Opaque,
    Blend,
    Mask
};

enum MediumType
{
    None,
    Absorb,
    Scatter,
    Emissive
};

class Material
{
public:
    Material()
    {
        baseColor = Vector(1.0f, 1.0f, 1.0f);
        anisotropic = 0.0f;

        emission = Vector(0.0f, 0.0f, 0.0f);
        lightId = -1;

        metallic = 0.0f;
        roughness = 0.5f;
        subsurface = 0.0f;
        specularTint = 0.0f;

        sheen = 0.0f;
        sheenTint = 0.0f;
        clearcoat = 0.0f;
        clearcoatGloss = 0.0f;

        specTrans = 0.0f;
        ior = 1.5f;
        mediumType = 0.0f;
        mediumDensity = 0.0f;

        mediumColor = Vector(1.0f, 1.0f, 1.0f);
        mediumAnisotropy = 0.0f;

        baseColorTexId = -1;
        metallicRoughnessTexID = -1;
        normalmapTexID = -1;
        emissionmapTexID = -1;

        opacity = 1.0f;
        alphaMode = 0.0f;
        alphaCutoff = 0.0f;
        // padding2
    };

    Vector baseColor;
    float anisotropic;

    Vector emission;
    int32 lightId;

    float metallic;
    float roughness;
    float subsurface;
    float specularTint;

    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;

    float specTrans;
    float ior;
    float mediumType;
    float mediumDensity;

    Vector mediumColor;
    float mediumAnisotropy;

    int32 baseColorTexId;
    int32 metallicRoughnessTexID;
    int32 normalmapTexID;
    int32 emissionmapTexID;

    float opacity;
    float alphaMode;
    float alphaCutoff;
    float padding2;
};

class jPathTracingLoadData
{
public:
    jPathTracingLoadData() : initialized(false), dirty(true) {}
    ~jPathTracingLoadData() {}

    static jPathTracingLoadData* LoadPathTracingData(std::string InFilename);

    // Create path tracing scene for jEngine
    void CreateSceneFor_jEngine(class jGame* InGame);
    void ClearSceneFor_jEngine(class jGame* InGame);
    std::vector<class jObject*> SpawnObjects; 
    std::vector<class jLight*> SpawnLights;

    int32 AddMesh(const std::string& filename);
    int32 AddTexture(const std::string& filename);
    int32 AddMaterial(const Material& material);
    int32 AddMeshInstance(const MeshInstance& meshInstance);
    int32 AddLight(const Light& light);
    void AddCamera(const Vector& InPos, const Vector& InTarget, const Vector& InUp, float InFov, float InNear = 0.1f, float InFar = 5000.0f);
    void AddEnvMap(const std::string& filename);

    // Options
    jRenderOptions renderOptions;

    // Meshes
    std::vector<Mesh*> meshes;

    // Scene Mesh Data 
    std::vector<Indices> vertIndices;
    std::vector<Vector4> verticesUVX; // Vertex + texture Coord (u/s)
    std::vector<Vector4> normalsUVY; // Normal + texture Coord (v/t)
    std::vector<Matrix> transforms;

    // Materials
    std::vector<Material> materials;

    // Instances
    std::vector<MeshInstance> meshInstances;

    // Lights
    std::vector<Light> lights;

    // Environment Map
    //EnvironmentMap* envMap;

    // Camera
    jCamera Camera;

    // Texture Data
    std::vector<jTexture*> textures;

    bool initialized;
    bool dirty;
    // To check if scene elements need to be resent to GPU
    bool instancesModified = false;
    bool envMapModified = false;
};

extern jPathTracingLoadData* gPathTracingScene;