#include "pch.h"
#include "FileLoader/jEditorAssets.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <fstream>

namespace
{
    namespace fs = std::filesystem;
    using Json = nlohmann::json;
    void Text(const fs::path& path, const std::string& value)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary); file << value;
    }
    void JsonFile(const fs::path& path, const Json& value) { Text(path, value.dump(2)); }
    Json Load(const fs::path& path) { std::ifstream file(path); Json value; file >> value; return value; }
    void Pixel(const fs::path& path, bool green = false)
    {
        const std::vector<unsigned char> red{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,192,240,31,0,5,0,1,255,137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130};
        auto bytes = red;
        if (green)
        {
            const std::vector<unsigned char> replacement{99,96,248,207,240,31,0,4,1,1,255,113,235,71,229};
            std::copy(replacement.begin(), replacement.end(), bytes.begin() + 43);
        }
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    template <typename Predicate> bool Await(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
}

int RunEditorProjectTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        std::cout << (condition ? "PASS " : "FAIL ") << message << std::endl; failures += !condition;
    };
    const auto fixture = fs::temp_directory_path()
        / ("jengine-project-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path Path; ~Cleanup() { std::error_code ec; fs::remove_all(Path, ec); } } cleanup{fixture};
    fs::create_directories(fixture);
    const auto directory = fixture / "Project";
    std::string error;
    jEditorProject project;
    expect(project.Create(directory, "Portable editor project", error), "project creation writes an explicit manifest and content folders");
    const auto projectId = project.GetConfig().Id;
    expect(!project.Create(directory, "Overwrite", error) && project.GetConfig().Id == projectId,
        "creating an existing project fails without overwriting its identity");
    project.EditConfig().StartupScene = "Scenes/startup.jscene";
    project.EditConfig().Settings["render.scale"] = "1.0";
    expect(project.Save(error), "project settings and startup scene save atomically");
    jEditorProject opened;
    expect(opened.Open(directory / "project.jproject", error) && opened.GetConfig().Id == projectId
        && opened.GetConfig().Settings.at("render.scale") == "1.0", "project opening retains persistent ID and settings");
    for (const std::string value : {"../escape", "Assets/../../escape", "Assets/CON.txt", "Assets/trailing.",
        "C:\\escape", "Assets\\..\\escape", "/tmp/outside", "Assets/name:stream"})
        expect(!jEditorProject::ResolveSafe(directory, value, error), "project path validation rejects traversal or nonportable paths");
    project.EditConfig().AssetRoot = "../escape";
    expect(!project.Save(error), "invalid project settings cannot replace a saved manifest");
    project.EditConfig().AssetRoot = "Assets";
    expect(opened.Open(directory / "project.jproject", error) && opened.GetConfig().AssetRoot == "Assets",
        "failed settings save preserves the valid project on disk");
    JsonFile(fixture / "invalid.jproject", {{"schemaVersion", 7}});
    expect(!opened.Open(fixture / "invalid.jproject", error) && opened.GetConfig().Id == projectId,
        "failed project opening preserves the previous project");
    const auto recent = fixture / "UserState" / "recent.json";
    expect(project.RememberRecent(recent, error) && project.RememberRecent(recent, error)
        && jEditorProject::LoadRecent(recent, error).size() == 1,
        "recent projects use caller-selected storage and deduplicate an open project");

    {
        using jEditorAssetPath::FromUtf8;
        const auto example = fs::absolute(FromUtf8(__FILE__)).parent_path().parent_path().parent_path()
            / "examples/authoring-demo";
        const auto copy = fixture / "ShippedStudio";
        const std::vector<std::string> shippedFiles = {
            "project.jproject", "Scenes/Studio.jscene", "Assets/Models/CheckerPyramid.obj",
            "Assets/Models/CheckerPyramid.mtl", "Assets/Textures/StudioChecker.png",
            "Scenes/Studio.jscene.jasset", "Assets/Models/CheckerPyramid.obj.jasset",
            "Assets/Models/CheckerPyramid.mtl.jasset", "Assets/Textures/StudioChecker.png.jasset"};
        const auto readBytes = [](const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open shipped Studio fixture input");
            std::string bytes(std::istreambuf_iterator<char>(file), {});
            if (file.bad() || bytes.empty()) throw std::runtime_error("Cannot read nonempty Studio fixture input");
            return bytes;
        };
        std::map<std::string, std::string> sourceBytes;
        bool copied = true;
        try
        {
            // Never scan the checkout example: scans may update .jasset files.
            // Copy shipped source and metadata byte-for-byte into our own fixture.
            for (const auto& relative : shippedFiles)
            {
                const auto path = FromUtf8(relative);
                const auto bytes = readBytes(example / path);
                sourceBytes.emplace(relative, bytes);
                fs::create_directories((copy / path).parent_path());
                { std::ofstream file(copy / path, std::ios::binary); file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }
                if (readBytes(copy / path) != bytes) throw std::runtime_error("Studio fixture copy changed bytes");
            }
        }
        catch (const std::exception& exception) { copied = false; std::cout << "DETAIL " << exception.what() << std::endl; }
        expect(copied, "shipped Studio project scene OBJ MTL PNG and persistent metadata exist and copy verbatim");
        if (copied)
        {
            jEditorProject studio;
            const bool opened = studio.Open(copy / "project.jproject", error);
            const auto startup = opened ? jEditorProject::ResolveSafe(studio.GetRoot(), studio.GetConfig().StartupScene, error, true) : std::nullopt;
            expect(opened && startup == fs::canonical(copy / "Scenes/Studio.jscene"),
                "shipped Studio project opens its actual declared startup scene from the temporary copy");
            if (opened && startup)
            {
                jEditorAssetRegistry studioRegistry(studio);
                const bool scanned = studioRegistry.Scan(error);
                const auto* model = studioRegistry.FindBySourcePath("Assets/Models/CheckerPyramid.obj");
                const auto* material = studioRegistry.FindBySourcePath("Assets/Models/CheckerPyramid.mtl");
                const auto* texture = studioRegistry.FindBySourcePath("Assets/Textures/StudioChecker.png");
                const auto* scene = studioRegistry.FindBySourcePath(studio.GetConfig().StartupScene);
                expect(scanned && studioRegistry.GetAssets().size() == 4 && model && material && texture && scene
                    && model->Kind == jEditorAssetKind::Model && texture->Kind == jEditorAssetKind::Texture,
                    "shipped Studio catalog contains exactly four actual assets including its tracked OBJ source");
                const bool dependencies = model && material && texture && scene
                    && model->Dependencies == std::vector<std::string>{material->Id}
                    && material->Dependencies == std::vector<std::string>{texture->Id}
                    && std::find(scene->Dependencies.begin(), scene->Dependencies.end(), model->Id) != scene->Dependencies.end()
                    && std::find(scene->Dependencies.begin(), scene->Dependencies.end(), texture->Id) != scene->Dependencies.end()
                    && std::all_of(studioRegistry.GetAssets().begin(), studioRegistry.GetAssets().end(),
                        [](const auto& asset) { return !asset.Missing && asset.MissingDependencies.empty(); });
                expect(dependencies && studioRegistry.Resolve(model->Id, error) == fs::canonical(copy / "Assets/Models/CheckerPyramid.obj")
                    && studioRegistry.Resolve(texture->Id, error) == fs::canonical(copy / "Assets/Textures/StudioChecker.png"),
                    "shipped Studio scene model material and checker texture resolve their real transitive dependencies");
                bool referencesResolve = true; size_t modelReferences = 0;
                const auto document = Load(*startup);
                for (const auto& entity : document.at("entities"))
                {
                    const auto source = entity.value("sourceAsset", std::string());
                    if (source.empty()) continue;
                    ++modelReferences;
                    const auto* referenced = studioRegistry.FindBySourcePath(source);
                    const auto id = entity.value("assetId", std::string());
                    referencesResolve &= referenced && referenced->Kind == jEditorAssetKind::Model
                        && (id.empty() || referenced->Id == id) && studioRegistry.Resolve(referenced->Id, error).has_value();
                }
                expect(referencesResolve && modelReferences > 0,
                    "shipped Studio sourceAsset and persistent assetId references agree and resolve to actual model files");
                jEditorThumbnail checker;
                expect(texture && PrepareEditorThumbnail(copy / FromUtf8(texture->SourcePath), checker, error)
                    && checker.Width && checker.Height && checker.Rgba.size() == size_t(checker.Width) * checker.Height * 4,
                    "shipped Studio checker is a decodable real PNG after the verbatim fixture copy");
                studioRegistry.Shutdown();
            }
            bool originalUnchanged = true;
            for (const auto& [relative, bytes] : sourceBytes) originalUnchanged &= readBytes(example / FromUtf8(relative)) == bytes;
            expect(originalUnchanged, "Studio validation writes metadata only in its temporary copy and preserves all shipped bytes");
        }
    }

    {
        using jEditorAssetPath::FromUtf8;
        using jEditorAssetPath::ToUtf8;
        const std::string picture = "자산/그림/녹색 그림.png";
        const auto nativePicture = FromUtf8(picture);
        bool nativeCorrect = ToUtf8(nativePicture) == picture;
#ifdef _WIN32
        nativeCorrect &= nativePicture.native() == L"자산/그림/녹색 그림.png";
#else
        nativeCorrect &= nativePicture.native() == picture;
#endif
        expect(nativeCorrect, "UTF-8 authored paths convert to native filesystem characters and round-trip exactly");
        bool malformedRejected = true;
        for (const auto& invalid : std::vector<std::string>{"Assets/\xc0\xaf", "Assets/\xed\xa0\x80", "Assets/\xf4\x90\x80\x80", "Assets/\xe3\x81"})
            malformedRejected &= !jEditorProject::ResolveSafe(directory, invalid, error) && !error.empty();
        expect(malformedRejected, "malformed UTF-8 paths fail validation with a diagnostic on every platform");
        const auto unicodeRoot = fixture / FromUtf8("한글 프로젝트 🧪");
        jEditorProject unicode;
        expect(unicode.Create(unicodeRoot, "한글 프로젝트", error), "project creation accepts a native Unicode directory and UTF-8 project name");
        unicode.EditConfig().AssetRoot = "자산";
        unicode.EditConfig().StartupScene = "Scenes/시작 장면.jscene";
        fs::create_directories(unicodeRoot / FromUtf8("자산"));
        JsonFile(unicodeRoot / FromUtf8(unicode.GetConfig().StartupScene), {{"entities", Json::array({{{"sourceAsset", "자산/모델/삼각형.obj"}}})}});
        jEditorProject unicodeOpened;
        const auto unicodeRecent = fixture / FromUtf8("사용자 상태/최근 프로젝트.json");
        expect(unicode.Save(error) && unicodeOpened.Open(unicode.GetProjectFile(), error)
            && unicodeOpened.GetConfig().Name == "한글 프로젝트" && unicodeOpened.GetConfig().AssetRoot == "자산"
            && unicodeOpened.GetConfig().StartupScene == "Scenes/시작 장면.jscene"
            && unicode.RememberRecent(unicodeRecent, error)
            && jEditorProject::LoadRecent(unicodeRecent, error) == std::vector<fs::path>{unicode.GetProjectFile()}
            && Load(unicodeRecent)["projects"][0] == ToUtf8(unicode.GetProjectFile()),
            "project and recent JSON retain UTF-8 names and file paths without ANSI serialization");
        const auto external = fixture / FromUtf8("외부 그림/녹색 그림.png"); Pixel(external, true);
        Text(unicodeRoot / FromUtf8("자산/모델/삼각형.obj"), "mtllib 재질.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl green\nf 1 2 3\n");
        Text(unicodeRoot / FromUtf8("자산/모델/재질.mtl"), "newmtl green\nmap_Kd ../그림/녹색 그림.png\n");
        jEditorAssetRegistry unicodeRegistry(unicodeOpened);
        expect(unicodeRegistry.ImportFile(external, picture, error), "asset import copies native Unicode files into UTF-8 project-relative destinations");
        const auto* unicodeModel = unicodeRegistry.FindBySourcePath("자산/모델/삼각형.obj");
        const auto* unicodeTexture = unicodeRegistry.FindBySourcePath(picture);
        const auto* unicodeMaterial = unicodeRegistry.FindBySourcePath("자산/모델/재질.mtl");
        expect(unicodeModel && unicodeTexture && unicodeMaterial && unicodeMaterial->DependencyPaths == std::vector<std::string>{picture}
            && unicodeRegistry.Resolve(unicodeTexture->Id, error) == fs::canonical(unicodeRoot / nativePicture),
            "catalog dependency records and safe resolution preserve Korean model material and texture paths");
        const auto unicodeModelId = unicodeModel ? unicodeModel->Id : std::string();
        expect(unicodeRegistry.SetImportSettings(unicodeModelId, "{\"scale\":0.5}", error)
            && Load(unicodeRoot / FromUtf8("자산/모델/삼각형.obj.jasset"))["importSettings"]["scale"] == .5,
            "model import settings are persisted beside the actual Unicode source file");
        jEditorThumbnail unicodeThumbnail;
        expect(PrepareEditorThumbnail(unicodeRoot / nativePicture, unicodeThumbnail, error)
            && unicodeThumbnail.Rgba == std::vector<std::uint8_t>({0,255,0,255}),
            "native Unicode PNG paths decode the bounded byte snapshot into real thumbnail pixels");
        const auto emptyPng = fixture / FromUtf8("빈 그림.png"); Text(emptyPng, "");
        const auto hugePng = fixture / FromUtf8("너무 큰 그림.png");
        { std::ofstream file(hugePng, std::ios::binary); file.seekp(64 * 1024 * 1024); file.put(0); }
        expect(!PrepareEditorThumbnail(emptyPng, unicodeThumbnail, error) && unicodeThumbnail.Rgba.empty()
            && !PrepareEditorThumbnail(hugePng, unicodeThumbnail, error) && unicodeThumbnail.Rgba.empty(),
            "thumbnail snapshot rejects empty and oversized files before decoding and clears previous pixels");
        jEditorPackageRequest unicodePackage;
        unicodePackage.Project = &unicodeOpened; unicodePackage.Registry = &unicodeRegistry;
        unicodePackage.OutputDirectory = fixture / FromUtf8("내보내기/한글 패키지");
        unicodePackage.RuntimeExecutable = "runtime/실행 파일";
        const auto nativeRuntime = fixture / FromUtf8("엔진 자료/실행 파일"); Text(nativeRuntime, "explicit runtime fixture");
        unicodePackage.RuntimeFiles = {{nativeRuntime, unicodePackage.RuntimeExecutable}};
        jEditorPackageReport unicodeReport;
        expect(jEditorPackager::Build(unicodePackage, unicodeReport, error)
            && jEditorPackager::Validate(unicodeReport.Manifest, error)
            && fs::exists(unicodePackage.OutputDirectory / nativePicture)
            && Load(unicodeReport.Manifest)["scene"] == unicodeOpened.GetConfig().StartupScene,
            "package manifests and transitive copies preserve UTF-8 asset scene runtime and output paths");
        if (!unicodeReport.Manifest.empty())
        {
            const auto unicodeRelocated = fixture / FromUtf8("옮긴 패키지");
            fs::rename(unicodePackage.OutputDirectory, unicodeRelocated);
            expect(jEditorPackager::ResolveStartupScene(unicodeRelocated / "jengine-runtime.json", error)
                == fs::canonical(unicodeRelocated / FromUtf8(unicodeOpened.GetConfig().StartupScene)),
                "a relocated Unicode package resolves its Unicode scene from the manifest directory");
        }
        unicodeRegistry.Shutdown();
    }

    Pixel(directory / "Assets/Textures/albedo.png");
    Text(directory / "Assets/Models/triangle.obj", "mtllib triangle.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl test\nf 1 2 3\n");
    Text(directory / "Assets/Models/triangle.mtl", "newmtl test\nmap_Kd ../Textures/albedo.png\n");
    Pixel(directory / "Assets/Textures/Wood Floor's.png");
    Text(directory / "Assets/Models/spaces.mtl", "newmtl wood\nmap_Kd -o 0 0 0 ../Textures/Wood Floor's.png\n");
    Text(directory / "Assets/Data/geometry buffer.bin", "123456789012");
    JsonFile(directory / "Assets/Models/model.gltf", {{"asset", {{"version", "2.0"}}},
        {"buffers", Json::array({{{"uri", "../Data/geometry%20buffer.bin"}, {"byteLength", 12}}})},
        {"images", Json::array({{{"uri", "../Textures/albedo.png"}}})}});
    Text(directory / "Assets/Unused/note.txt", "Not included unless referenced");
    Text(directory / "Assets/.env", "must not be scanned or packaged");
    Text(directory / "Assets/Shaders/custom.hlsl", "#include \"includes/common.hlsli\"\n");
    Text(directory / "Assets/Shaders/includes/common.hlsli", "float4 Example() { return 1; }\n");
    JsonFile(directory / "Assets/Materials/graph.jmaterial", {{"nodes", Json::array({
        {{"id", 1}, {"kind", "Texture"}, {"texture", "Assets/Textures/albedo.png"}}})}, {"output", 1}});
    Json scene{{"schemaVersion", 1}, {"entities", Json::array({
        {{"sourceAsset", "Assets/Models/triangle.obj"}, {"assetId", ""}},
        {{"sourceAsset", "Assets/Models/model.gltf"}, {"assetId", ""}},
        {{"material", {{"graphAsset", "Assets/Materials/graph.jmaterial"}}}}
    })}};
    JsonFile(directory / "Scenes/startup.jscene", scene);
    jEditorAssetRegistry registry(project);
    expect(registry.Scan(error), "recursive catalog scans real OBJ MTL glTF graph shader and scene sources");
    const auto* model = registry.FindBySourcePath("Assets/Models/triangle.obj");
    const auto* texture = registry.FindBySourcePath("Assets/Textures/albedo.png");
    const auto* gltf = registry.FindBySourcePath("Assets/Models/model.gltf");
    const auto* shader = registry.FindBySourcePath("Assets/Shaders/custom.hlsl");
    expect(model && texture && gltf && shader && !registry.FindBySourcePath("Assets/.env"),
        "catalog classifies assets and excludes secret or runtime state files");
    if (!model || !texture || !gltf || !shader) return failures + 1;
    const auto modelId = model->Id, textureId = texture->Id, gltfId = gltf->Id;
    const auto oldTextureRevision = texture->Revision, oldModelRevision = model->Revision;
    expect(model->Dependencies.size() == 1 && gltf->Dependencies.size() == 2 && shader->Dependencies.size() == 1,
        "dependency extraction resolves OBJ material glTF percent-encoded buffers and shader includes");
    const auto* material = registry.FindBySourcePath("Assets/Models/triangle.mtl");
    expect(material && material->Dependencies == std::vector<std::string>{textureId},
        "MTL texture references become stable transitive asset IDs");
    const auto* spaces = registry.FindBySourcePath("Assets/Models/spaces.mtl");
    const auto* spacedTexture = registry.FindBySourcePath("Assets/Textures/Wood Floor's.png");
    expect(spaces && spacedTexture && spaces->Dependencies == std::vector<std::string>{spacedTexture->Id},
        "MTL map options and unquoted texture filename spaces preserve the real dependency");
    expect(registry.Scan(error) && registry.FindById(modelId)->Revision == oldModelRevision,
        "unchanged rescans preserve stable identities and content revisions");
    jEditorAssetRegistry restored(opened);
    expect(restored.Scan(error) && restored.FindBySourcePath("Assets/Models/triangle.obj")->Id == modelId,
        "a new registry process reads asset identity from persistent sidecars");
    const auto* note = registry.FindBySourcePath("Assets/Unused/note.txt");
    const auto noteId = note ? note->Id : std::string();
    expect(registry.CreateFolder("Assets/Renamed", error)
        && registry.MovePath(registry.FindById(noteId)->SourcePath, "Assets/Renamed/note.txt", error)
        && registry.FindById(noteId) && registry.FindById(noteId)->SourcePath == "Assets/Renamed/note.txt",
        "asset moves preserve UUID metadata independently of display position or filename");
    expect(!registry.MovePath("Assets/Textures/albedo.png", "Assets/Textures/moved.png", error)
        && fs::exists(directory / "Assets/Textures/albedo.png"),
        "moves that would break inbound source dependencies leave files untouched");
    Pixel(directory / "Assets/Textures/albedo.png", true);
    std::vector<std::string> invalidated;
    expect(registry.Reimport(registry.FindById(textureId)->Id, invalidated, error)
        && std::find(invalidated.begin(), invalidated.end(), modelId) != invalidated.end()
        && std::find(invalidated.begin(), invalidated.end(), gltfId) != invalidated.end()
        && registry.FindById(textureId)->Revision > oldTextureRevision
        && registry.FindById(modelId)->Revision > oldModelRevision,
        "reimport changes source and transitive dependent revisions for runtime cache replacement");
    expect(registry.SetImportSettings(registry.FindById(textureId)->Id, "{\"sRGB\":true,\"generateMips\":true}", error)
        && Load(directory / "Assets/Textures/albedo.png.jasset")["importSettings"]["sRGB"] == true,
        "typed import settings persist in asset metadata");
    expect(!registry.SetImportSettings(textureId, "[]", error), "non-object import settings are rejected");
    jEditorThumbnail thumbnail;
    expect(PrepareEditorThumbnail(directory / "Assets/Textures/albedo.png", thumbnail, error)
        && thumbnail.Width == 1 && thumbnail.Height == 1 && thumbnail.Rgba == std::vector<std::uint8_t>({0,255,0,255}),
        "real PNG thumbnail preparation returns owned CPU RGBA pixels");
    expect(!PrepareEditorThumbnail(directory / "Assets/Textures/albedo.png", thumbnail, error, 1024)
        && thumbnail.Rgba.empty(), "thumbnail output dimensions are bounded and invalid requests clear old pixels");

    const auto unreferenced = directory / "Assets/Renamed/note.txt";
    fs::remove(unreferenced);
    expect(registry.Scan(error) && registry.FindById(noteId)->Missing && !registry.Resolve(noteId, error),
        "missing sources retain sidecar identity but cannot resolve as usable assets");
    Text(unreferenced, "Restored asset");
    expect(registry.Scan(error) && !registry.FindById(noteId)->Missing,
        "restoring a source reconnects its original persistent identity");
    fs::path trash;
    expect(registry.TrashPath(registry.FindById(noteId)->SourcePath, trash, error) && fs::exists(trash)
        && !fs::exists(unreferenced) && !registry.FindById(noteId), "asset deletion uses recoverable project-local trash");
    const auto metadataPath = directory / "Assets/Models/triangle.obj.jasset";
    const auto metadata = Load(metadataPath);
    auto broken = metadata; broken["id"] = textureId; JsonFile(metadataPath, broken);
    const auto revisionBeforeFailure = registry.GetRevision();
    expect(!registry.Scan(error) && registry.GetRevision() == revisionBeforeFailure && registry.FindById(modelId),
        "duplicate persistent IDs fail scanning without replacing the live catalog");
    JsonFile(metadataPath, metadata);
    expect(registry.Scan(error), "corrected metadata can be rescanned after a failed snapshot");

    std::error_code symlinkError;
    Text(fixture / "Outside/private.txt", "outside");
    fs::create_directory_symlink(fixture / "Outside", directory / "Assets/escape", symlinkError);
    if (!symlinkError)
        expect(!jEditorProject::ResolveSafe(directory, "Assets/escape/private.txt", error),
            "project path resolution rejects symlink escapes");
    else std::cout << "SKIP symlink fixture unavailable on this host" << std::endl;

    const auto obsolete = registry.ScanAsync();
    const auto latest = registry.ScanAsync();
    bool published = false;
    expect(obsolete && latest > obsolete && Await([&] {
        const auto result = registry.PollScan();
        if (result && result->Generation == latest) { published = result->Succeeded; return true; }
        return false;
    }) && published, "bounded asynchronous scans discard stale work and publish the latest generation");
    auto wrongOwner = std::async(std::launch::async, [&] { return !registry.ScanAsync() && !registry.PollScan(); });
    expect(wrongOwner.get(), "asset catalog mutations require the creating owner thread");

    // Replace a source-path reference by its stable identity. A stale optional
    // source hint must not break packaging when the ID still resolves.
    scene["entities"][0]["assetId"] = modelId;
    scene["entities"][0]["sourceAsset"] = "Assets/OldName/triangle.obj";
    JsonFile(directory / "Scenes/startup.jscene", scene);
    expect(registry.Scan(error), "scene dependency lookup prefers persistent identity over a stale display path");
    const auto runtime = fixture / "ExplicitRuntimeInputs";
    Text(runtime / "jEngineFixture", "a runtime binary fixture, not executable engine code");
    Text(runtime / "Shaders/Basic.hlsl", "float4 Main() { return 1; }");
    Text(runtime / "Shaders/.env", "must not be copied");
    Text(runtime / "libs/dependency.dylib", "explicit runtime dependency");
    jEditorPackageRequest package;
    package.Project = &project; package.Registry = &registry;
    package.OutputDirectory = fixture / "Package";
    package.StartupScene = "Scenes/startup.jscene";
    package.RuntimeExecutable = "runtime/jEngineFixture";
    package.RuntimeFiles = {{runtime / "jEngineFixture", "runtime/jEngineFixture"},
        {runtime / "Shaders", "Shaders"}, {runtime / "libs", "runtime/libs"}};
    jEditorPackageReport report;
    expect(jEditorPackager::Build(package, report, error), "runtime package stages and validates explicit runtime inputs plus transitive assets");
    if (report.Manifest.empty()) { std::cout << "Package error: " << error << std::endl; return failures + 1; }
    expect(fs::exists(package.OutputDirectory / "Assets/Models/triangle.mtl")
        && fs::exists(package.OutputDirectory / "Assets/Textures/albedo.png")
        && fs::exists(package.OutputDirectory / "Assets/Data/geometry buffer.bin")
        && fs::exists(package.OutputDirectory / "Assets/Materials/graph.jmaterial")
        && fs::exists(package.OutputDirectory / "Shaders/Basic.hlsl")
        && fs::exists(package.OutputDirectory / "runtime/libs/dependency.dylib"),
        "package includes real model material texture graph glTF buffer shader and runtime dependencies");
    expect(!fs::exists(package.OutputDirectory / "Assets/.env") && !fs::exists(package.OutputDirectory / "Shaders/.env")
        && !fs::exists(package.OutputDirectory / "Assets/Unused/note.txt"),
        "package excludes secrets and unreferenced assets");
    const auto relocated = fixture / "Relocated/Package";
    fs::create_directories(relocated.parent_path()); fs::rename(package.OutputDirectory, relocated);
    const auto relocatedManifest = relocated / "jengine-runtime.json";
    expect(jEditorPackager::Validate(relocatedManifest, error)
        && jEditorPackager::ResolveStartupScene(relocatedManifest, error) == fs::canonical(relocated / "Scenes/startup.jscene"),
        "runtime package relocates and resolves its startup scene from the manifest directory");
    const auto preservedManifest = Load(relocatedManifest);
    auto unsafeManifest = preservedManifest; unsafeManifest["files"][0]["path"] = "../Outside/private.txt";
    JsonFile(relocatedManifest, unsafeManifest);
    expect(!jEditorPackager::Validate(relocatedManifest, error), "runtime manifest rejects traversal even when the outside file exists");
    JsonFile(relocatedManifest, preservedManifest);
    Text(relocated / "Assets/Textures/albedo.png", "tampered");
    expect(!jEditorPackager::Validate(relocatedManifest, error), "runtime package validates copied asset content fingerprints");
    Pixel(relocated / "Assets/Textures/albedo.png", true);
    fs::remove(relocated / "runtime/libs/dependency.dylib");
    expect(!jEditorPackager::Validate(relocatedManifest, error), "missing runtime dependencies fail package validation");
    package.OutputDirectory = fixture / "MissingPackage";
    fs::remove(directory / "Assets/Data/geometry buffer.bin");
    expect(registry.Scan(error) && !jEditorPackager::Build(package, report, error) && !fs::exists(package.OutputDirectory),
        "missing transitive source dependency fails before a package output is published");
    registry.ScanAsync(); registry.CancelScan(); registry.Shutdown();
    expect(!registry.ScanAsync() && !registry.GetWorkStats().Active,
        "registry shutdown cancels and joins its CPU worker without retaining unbounded results");
    return failures;
}
