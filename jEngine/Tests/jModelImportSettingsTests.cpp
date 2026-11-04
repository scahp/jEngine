#include "pch.h"
#include "FileLoader/jModelLoader.h"
#include "FileLoader/jAssetImportService.h"
#include "FileLoader/jEditorAssetPath.h"
#include "FileLoader/jAssimpUtf8IO.h"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <fstream>

int RunModelImportSettingsTests()
{
    namespace fs = std::filesystem;
    using Json = nlohmann::json;
    using jEditorAssetPath::FromUtf8;
    using jEditorAssetPath::ToUtf8;
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl; failures += !condition;
    };
    const auto folder = fs::temp_directory_path()
        / FromUtf8("jengine-가져오기-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup { fs::path Folder; ~Cleanup() { std::error_code ec; fs::remove_all(Folder, ec); } } cleanup{folder};
    const auto model = folder / FromUtf8("한글 모델.obj");
    const auto metadata = FromUtf8(ToUtf8(model) + ".jasset");
    { std::ofstream file(model); file << "mtllib 한글재질.mtl\nusemtl test\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.25 0.2\nvt 0.6 0.2\nvt 0.25 0.8\nf 1/1 2/2 3/3\n"; }
    { std::ofstream file(folder / FromUtf8("한글재질.mtl")); file << "newmtl test\nKd 0.2 0.7 0.3\n"; }
    const auto settings = [&](const Json& options) {
        std::ofstream file(metadata);
        file << Json{{"schemaVersion", 1}, {"id", "00000000-0000-4000-8000-000000000001"},
            {"revision", 1}, {"fingerprint", {{"size", 0}, {"modified", 0}, {"hash", ""}}}, {"importSettings", options}}.dump(2);
    };
    const auto bytes = [&] { std::ifstream file(metadata, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); };
    auto& loader = jModelLoader::GetInstance();
    std::string error;
    auto prepare = [&] { return loader.PrepareModel(ToUtf8(model).c_str(), ToUtf8(folder).c_str(), {}, &error); };
    {
        Assimp::Importer importer;
        importer.SetIOHandler(new jAssimpUtf8IO());
        const auto* scene = importer.ReadFile(ToUtf8(model), aiProcess_Triangulate);
        bool materialRead = false;
        if (scene) for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        {
            aiColor3D color;
            if (scene->mMaterials[i]->Get(AI_MATKEY_COLOR_DIFFUSE, color) == aiReturn_SUCCESS)
                materialRead |= std::abs(color.r - .2f) < .001f && std::abs(color.g - .7f) < .001f && std::abs(color.b - .3f) < .001f;
        }
        expect(materialRead, "UTF-8 model import opens the real external Korean MTL and reads its material data");
    }
    auto legacy = prepare();
    float position[3]{}, uv[2]{}, normal[3]{};
    expect(legacy && legacy->GetVertexPosition(1, position) && std::abs(position[0] - 1) < .001f
        && legacy->GetVertexTexCoord(0, uv) && std::abs(uv[1] - .8f) < .001f,
        "models without sidecars keep legacy scale and flipped UV preparation");
    expect(legacy && legacy->GetVertexNormal(0, normal) && std::abs(normal[2]) > .9f,
        "legacy model preparation generates missing surface normals");
    settings({{"scale", 2.5}, {"flipUVs", false}, {"generateNormals", false}});
    const auto before = bytes();
    auto configured = prepare();
    expect(configured && configured->GetVertexPosition(1, position) && std::abs(position[0] - 2.5f) < .001f
        && configured->GetVertexPosition(2, position) && std::abs(position[1] - 2.5f) < .001f,
        "model import scale changes actual prepared CPU vertex positions");
    expect(configured && configured->GetVertexTexCoord(0, uv) && std::abs(uv[1] - .2f) < .001f,
        "disabled UV flipping changes actual prepared texture coordinates");
    expect(configured && configured->GetVertexNormal(0, normal) && std::abs(normal[1] - 1) < .001f && std::abs(normal[2]) < .001f,
        "disabled normal generation retains the defined missing-normal fallback");
    expect(bytes() == before, "CPU import reads settings without modifying persistent metadata");
    expect(configured && !configured->GetVertexPosition(100, position) && !configured->GetVertexTexCoord(100, uv)
        && !configured->GetVertexNormal(100, normal) && !configured->GetVertexPosition(0, nullptr),
        "prepared CPU inspection rejects invalid indices and null destinations");
    bool invalid = true;
    for (const auto& options : std::vector<Json>{
        {{"scale", 0}}, {{"scale", -1}}, {{"scale", "2"}}, {{"scale", 1e100}},
        {{"flipUVs", 0}}, {{"generateNormals", "true"}}, {{"unsupported", true}}})
    {
        settings(options); invalid &= !prepare() && !error.empty();
    }
    expect(invalid, "invalid or unsupported model import settings fail preparation with a diagnostic");
    settings({{"scale", -2}});
    jAssetImportService service;
    const auto request = service.Request(ToUtf8(model), ToUtf8(folder));
    std::optional<jAssetImportService::Result> result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!result && std::chrono::steady_clock::now() < deadline)
    { result = service.Poll(); if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    expect(result && result->Id == request && result->State == jAssetImportService::Status::Failed
        && result->Error.find("scale") != std::string::npos,
        "invalid import settings publish a terminal asynchronous failure");
    const auto malformed = service.Request(ToUtf8(folder) + "/\xc0\xaf.obj", ToUtf8(folder));
    result.reset();
    const auto malformedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!result && std::chrono::steady_clock::now() < malformedDeadline)
    { result = service.Poll(); if (!result) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    expect(result && result->Id == malformed && result->State == jAssetImportService::Status::Failed
        && result->Error.find("UTF-8") != std::string::npos,
        "malformed UTF-8 model paths publish a terminal failure instead of escaping worker result guards");
    service.Shutdown();
    settings({{"scale", .25}, {"flipUVs", true}, {"generateNormals", true}});
    auto retried = prepare();
    expect(retried && retried->GetVertexPosition(1, position) && std::abs(position[0] - .25f) < .001f,
        "correcting model import settings allows the same source to retry successfully");
    return failures;
}
