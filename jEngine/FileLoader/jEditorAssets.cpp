#include "pch.h"
#include "jEditorAssets.h"
#include "jAssimpUtf8IO.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "stb_image.h"
#include <atomic>
#include <cctype>
#include <fstream>
#include <future>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>

namespace
{
    namespace fs = std::filesystem;
    using Json = nlohmann::json;
    using jEditorAssetPath::FromUtf8;
    using jEditorAssetPath::ToUtf8;
    using Cancel = std::shared_ptr<std::atomic<bool>>;
    constexpr size_t MaxJsonBytes = 16 * 1024 * 1024;
    constexpr size_t MaxAssets = 50000;
    constexpr size_t MaxDiagnostics = 256;

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    std::string NewId()
    {
        std::random_device random;
        std::array<unsigned char, 16> bytes{};
        for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
        bytes[6] = (bytes[6] & 15) | 64;
        bytes[8] = (bytes[8] & 63) | 128;
        std::ostringstream text;
        text << std::hex << std::setfill('0');
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10) text << '-';
            text << std::setw(2) << unsigned(bytes[i]);
        }
        return text.str();
    }

    bool ValidId(const std::string& id)
    {
        if (id.size() != 36) return false;
        for (size_t i = 0; i < id.size(); ++i)
            if (i == 8 || i == 13 || i == 18 || i == 23)
            { if (id[i] != '-') return false; }
            else if (!std::isxdigit(static_cast<unsigned char>(id[i]))) return false;
        return true;
    }

    bool Within(const fs::path& root, const fs::path& value)
    {
        auto a = root.begin(), b = value.begin();
        for (; a != root.end(); ++a, ++b)
            if (b == value.end() || *a != *b) return false;
        return true;
    }

    bool Excluded(const fs::path& path)
    {
        for (const auto& part : path)
        {
            const auto name = Lower(ToUtf8(part));
            if (name == ".git" || name == ".jaeho-studio" || name == ".jeditor" || name == "build"
                || name == "cache" || name == "caches" || name == "logs" || name == "node_modules"
                || name == ".ds_store" || name.rfind(".env", 0) == 0) return true;
        }
        const auto extension = Lower(ToUtf8(path.extension()));
        return extension == ".pem" || extension == ".key" || extension == ".pfx" || extension == ".p12";
    }

    bool ReadJson(const fs::path& path, Json& value, std::string& error)
    {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec || size > MaxJsonBytes) { error = "JSON file missing, unreadable or too large: " + ToUtf8(path); return false; }
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Cannot open " + ToUtf8(path); return false; }
        std::string text(static_cast<size_t>(size), '\0');
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (static_cast<size_t>(input.gcount()) != text.size()) { error = "Short read: " + ToUtf8(path); return false; }
        try
        {
            value = Json::parse(text, [](int depth, Json::parse_event_t, Json&) {
                if (depth > 128) throw std::runtime_error("JSON nesting exceeds 128 levels");
                return true;
            }, false);
        }
        catch (const std::exception& exception) { error = ToUtf8(path) + ": " + exception.what(); return false; }
        if (value.is_discarded() || !value.is_object()) { error = "Invalid JSON object: " + ToUtf8(path); return false; }
        return true;
    }

    bool AtomicWrite(const fs::path& path, const Json& json, std::string& error)
    {
        const auto temporary = FromUtf8(ToUtf8(path) + ".tmp-" + NewId());
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) { error = "Cannot create metadata directory: " + ec.message(); return false; }
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            const auto text = json.dump(2) + "\n";
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            if (!output)
            {
                error = "Cannot write " + ToUtf8(path);
                output.close(); fs::remove(temporary, ec); return false;
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        { error = "Cannot publish " + ToUtf8(path); fs::remove(temporary, ec); return false; }
#else
        fs::rename(temporary, path, ec);
        if (ec) { error = "Cannot publish " + ToUtf8(path) + ": " + ec.message(); fs::remove(temporary, ec); return false; }
#endif
        return true;
    }

    bool Fingerprint(const fs::path& path, jEditorAssetFingerprint& out, std::string& error, const Cancel& cancel = {})
    {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec) { error = "Cannot stat asset: " + ToUtf8(path); return false; }
        const auto modified = fs::last_write_time(path, ec);
        if (ec) { error = "Cannot read asset timestamp: " + ToUtf8(path); return false; }
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "Cannot read asset: " + ToUtf8(path); return false; }
        std::array<char, 65536> block{};
        std::uint64_t hash = 14695981039346656037ull;
        std::uintmax_t count = 0;
        while (input)
        {
            if (cancel && cancel->load()) { error = "Cancelled"; return false; }
            input.read(block.data(), block.size());
            const auto read = input.gcount(); count += static_cast<std::uintmax_t>(read);
            for (std::streamsize i = 0; i < read; ++i)
            { hash ^= static_cast<unsigned char>(block[static_cast<size_t>(i)]); hash *= 1099511628211ull; }
        }
        const auto endTime = fs::last_write_time(path, ec);
        if (!input.eof() || count != size || ec || endTime != modified)
        { error = "Asset changed or failed while reading: " + ToUtf8(path); return false; }
        std::ostringstream text; text << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash;
        out = {size, static_cast<std::int64_t>(modified.time_since_epoch().count()), text.str()};
        return true;
    }

    Json FingerprintJson(const jEditorAssetFingerprint& value)
    { return {{"size", value.Size}, {"modified", value.Modified}, {"hash", value.Hash}}; }

    jEditorAssetKind Kind(const fs::path& path)
    {
        const auto ext = Lower(ToUtf8(path.extension()));
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".dae" || ext == ".ply") return jEditorAssetKind::Model;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".hdr" || ext == ".tga" || ext == ".bmp") return jEditorAssetKind::Texture;
        if (ext == ".mtl" || ext == ".jmaterial") return jEditorAssetKind::Material;
        if (ext == ".jscene" || ext == ".jprefab" || ext == ".prefab" || ext == ".scene") return jEditorAssetKind::Scene;
        if (ext == ".hlsl" || ext == ".hlsli" || ext == ".vert" || ext == ".frag" || ext == ".glsl") return jEditorAssetKind::Shader;
        return jEditorAssetKind::Data;
    }

    fs::path Sidecar(const fs::path& source) { return FromUtf8(ToUtf8(source) + ".jasset"); }

    Json Metadata(const jEditorAssetRecord& asset)
    {
        return {{"schemaVersion", 1}, {"id", asset.Id}, {"revision", asset.Revision},
            {"fingerprint", FingerprintJson(asset.Fingerprint)}, {"importSettings", Json::parse(asset.ImportSettingsJson)},
            {"dependencies", asset.Dependencies}, {"dependencyPaths", asset.DependencyPaths}};
    }

    bool ReadMetadata(const fs::path& path, jEditorAssetRecord& asset, std::string& error)
    {
        std::error_code ec;
        if (fs::is_symlink(fs::symlink_status(path, ec))) { error = "Asset metadata cannot be a symlink: " + ToUtf8(path); return false; }
        Json json;
        if (!ReadJson(path, json, error)) return false;
        try
        {
            if (json.at("schemaVersion").get<int>() != 1) throw std::runtime_error("Unsupported metadata version");
            asset.Id = json.at("id").get<std::string>();
            if (!ValidId(asset.Id)) throw std::runtime_error("Invalid persistent asset ID");
            asset.Revision = json.at("revision").get<std::uint64_t>();
            if (!asset.Revision) throw std::runtime_error("Invalid asset revision");
            const auto& fp = json.at("fingerprint");
            asset.Fingerprint = {fp.at("size").get<std::uintmax_t>(), fp.at("modified").get<std::int64_t>(), fp.at("hash").get<std::string>()};
            const auto settings = json.value("importSettings", Json::object());
            if (!settings.is_object()) throw std::runtime_error("Import settings must be an object");
            asset.ImportSettingsJson = settings.dump();
            asset.Dependencies = json.value("dependencies", std::vector<std::string>{});
            asset.DependencyPaths = json.value("dependencyPaths", std::vector<std::string>{});
            return true;
        }
        catch (const std::exception& exception) { error = ToUtf8(path) + ": " + exception.what(); return false; }
    }

    std::vector<std::string> Words(const std::string& line)
    {
        std::vector<std::string> result; std::string word; char quote = 0;
        for (char c : line)
        {
            if (!quote && c == '#') break;
            if (c == '\'' || c == '"')
            { if (!quote && word.empty()) quote = c; else if (quote == c) quote = 0; else word += c; }
            else if (!quote && std::isspace(static_cast<unsigned char>(c)))
            { if (!word.empty()) { result.push_back(word); word.clear(); } }
            else word += c;
        }
        if (!word.empty()) result.push_back(word);
        return result;
    }

    std::string DecodeUri(const std::string& uri)
    {
        std::string result;
        for (size_t i = 0; i < uri.size(); ++i)
        {
            if (uri[i] == '%' && i + 2 < uri.size()
                && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) && std::isxdigit(static_cast<unsigned char>(uri[i + 2])))
            { result += char(std::strtoul(uri.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
            else result += uri[i];
        }
        return result;
    }

    std::string MaterialTextureFilename(const std::vector<std::string>& words,
        const fs::path& root, const fs::path& relative)
    {
        const auto suffix = [&](size_t begin) {
            std::string value;
            for (size_t i = begin; i < words.size(); ++i) { if (!value.empty()) value += ' '; value += words[i]; }
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        };
        // Prefer an existing safe filename suffix, preserving unquoted spaces
        // and preceding MTL map options in existing authoring files.
        for (size_t i = 1; i < words.size(); ++i)
        {
            const auto value = suffix(i);
            std::string error;
            const auto path = jEditorProject::ResolveSafe(root, (relative.parent_path() / FromUtf8(value)).lexically_normal(), error);
            std::error_code ec;
            if (path && fs::is_regular_file(*path, ec)) return value;
        }
        size_t index = 1;
        while (index < words.size() && words[index].rfind("-", 0) == 0)
        {
            const auto option = Lower(words[index++]);
            if (option == "-o" || option == "-s" || option == "-t")
            {
                for (int count = 0; count < 3 && index < words.size(); ++count)
                {
                    char* end = nullptr; std::strtod(words[index].c_str(), &end);
                    if (end == words[index].c_str() || *end) break;
                    ++index;
                }
            }
            else index = std::min(words.size(), index + (option == "-mm" ? size_t(2) : size_t(1)));
        }
        return index < words.size() ? suffix(index) : words.back();
    }

    struct References { std::vector<std::string> Paths, Ids; };
    void JsonReferences(const Json& json, References& refs)
    {
        if (json.is_array()) { for (const auto& item : json) JsonReferences(item, refs); return; }
        if (!json.is_object()) return;
        bool hasId = false;
        for (auto it = json.begin(); it != json.end(); ++it)
            if (Lower(it.key()) == "assetid" && it.value().is_string() && !it.value().get<std::string>().empty()) hasId = true;
        for (auto it = json.begin(); it != json.end(); ++it)
        {
            const auto key = Lower(it.key());
            if (it.value().is_string())
            {
                const auto value = it.value().get<std::string>();
                if (value.empty()) continue;
                if (key == "assetid") refs.Ids.push_back(value);
                else if (key == "sourceasset" || key == "asset" || key == "basecolortexture" || key == "normaltexture"
                    || key == "graphasset" || key == "texture")
                {
                    if (key == "sourceasset" && hasId) continue;
                    if (ValidId(value)) refs.Ids.push_back(value); else refs.Paths.push_back(value);
                }
            }
            else JsonReferences(it.value(), refs);
        }
    }

    bool SourceReferences(const fs::path& root, const fs::path& relative, References& refs, std::string& error)
    {
        const auto path = root / relative;
        const auto extension = Lower(ToUtf8(path.extension()));
        if (extension == ".jscene" || extension == ".jprefab" || extension == ".prefab" || extension == ".jmaterial")
        {
            Json json; if (!ReadJson(path, json, error)) return false;
            JsonReferences(json, refs); // Authored references are project-relative.
            return true;
        }
        std::vector<std::string> local;
        if (extension == ".gltf")
        {
            Json json; if (!ReadJson(path, json, error)) return false;
            for (const char* key : {"buffers", "images"})
                if (json.contains(key) && json[key].is_array())
                    for (const auto& item : json[key])
                        if (item.is_object() && item.contains("uri") && item["uri"].is_string())
                            local.push_back(DecodeUri(item["uri"].get<std::string>()));
        }
        if (extension == ".obj" || extension == ".mtl" || Kind(path) == jEditorAssetKind::Shader)
        {
            std::ifstream input(path); if (!input) { error = "Cannot read source dependencies: " + ToUtf8(path); return false; }
            std::string line;
            while (std::getline(input, line))
            {
                const auto words = Words(line);
                if (extension == ".obj" && words.size() > 1 && words[0] == "mtllib")
                    local.insert(local.end(), words.begin() + 1, words.end());
                else if (extension == ".mtl" && words.size() > 1
                    && (Lower(words[0]).rfind("map_", 0) == 0 || Lower(words[0]) == "bump" || Lower(words[0]) == "disp" || Lower(words[0]) == "decal"))
                    local.push_back(MaterialTextureFilename(words, root, relative));
                else if (Kind(path) == jEditorAssetKind::Shader)
                {
                    const auto include = line.find("#include");
                    const auto begin = include == std::string::npos ? include : line.find('"', include);
                    const auto end = begin == std::string::npos ? begin : line.find('"', begin + 1);
                    if (begin != std::string::npos && end != std::string::npos) local.push_back(line.substr(begin + 1, end - begin - 1));
                }
            }
        }
        // Assimp supplies external texture references for FBX/DAE/GLB as well.
        if (extension == ".fbx" || extension == ".dae" || extension == ".glb")
        {
            Assimp::Importer importer;
            importer.SetIOHandler(new jAssimpUtf8IO());
            const auto* scene = importer.ReadFile(ToUtf8(path), 0);
            if (!scene) { error = "Cannot inspect model dependencies: " + ToUtf8(path) + ": " + importer.GetErrorString(); return false; }
            for (unsigned m = 0; m < scene->mNumMaterials; ++m)
                for (unsigned type = aiTextureType_DIFFUSE; type <= aiTextureType_UNKNOWN; ++type)
                    for (unsigned index = 0; index < scene->mMaterials[m]->GetTextureCount(static_cast<aiTextureType>(type)); ++index)
                    { aiString name; if (scene->mMaterials[m]->GetTexture(static_cast<aiTextureType>(type), index, &name) == AI_SUCCESS) local.emplace_back(name.C_Str()); }
        }
        for (auto value : local)
        {
            if (value.empty() || value[0] == '*' || value.rfind("data:", 0) == 0) continue;
            std::replace(value.begin(), value.end(), '\\', '/');
            if (value.find(":") != std::string::npos || FromUtf8(value).is_absolute())
            { error = "External/absolute dependency is not portable: " + value; return false; }
            const auto dependency = (relative.parent_path() / FromUtf8(value)).lexically_normal();
            std::string pathError;
            if (!jEditorProject::ResolveSafe(root, dependency, pathError)) { error = "Unsafe dependency " + value + ": " + pathError; return false; }
            refs.Paths.push_back(ToUtf8(dependency));
        }
        return true;
    }
}

const char* jEditorAssetKindName(jEditorAssetKind kind)
{
    switch (kind)
    {
    case jEditorAssetKind::Model: return "Model";
    case jEditorAssetKind::Texture: return "Texture";
    case jEditorAssetKind::Material: return "Material";
    case jEditorAssetKind::Scene: return "Scene";
    case jEditorAssetKind::Shader: return "Shader";
    default: return "Data";
    }
}

std::optional<fs::path> jEditorProject::ResolveSafe(const fs::path& root, const fs::path& relative,
    std::string& error, bool mustExist)
{
    error.clear();
    auto text = ToUtf8(relative);
    std::replace(text.begin(), text.end(), '\\', '/');
    const auto path = FromUtf8(text);
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    { error = "A project-relative path is required"; return std::nullopt; }
    for (const auto& part : path)
    {
        const auto segment = ToUtf8(part);
        if (segment == ".." || segment.empty() || segment.back() == ' ' || segment.back() == '.'
            || segment.find_first_of(":<>\"|?*") != std::string::npos
            || std::any_of(segment.begin(), segment.end(), [](unsigned char c) { return c < 32; }))
        { error = "Unsafe or nonportable path segment"; return std::nullopt; }
        const auto stem = Lower(ToUtf8(FromUtf8(segment).stem()));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
            || (stem.size() == 4 && (stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) && stem[3] >= '1' && stem[3] <= '9'))
        { error = "Reserved path segment"; return std::nullopt; }
    }
    std::error_code ec;
    const auto canonicalRoot = fs::weakly_canonical(fs::absolute(root, ec), ec);
    if (ec || canonicalRoot.empty()) { error = "Invalid project root"; return std::nullopt; }
    auto target = canonicalRoot;
    for (const auto& part : path)
    {
        target /= part;
        const auto status = fs::symlink_status(target, ec);
        if (!ec && fs::is_symlink(status)) { error = "Symlinks are not allowed in project paths"; return std::nullopt; }
        if (ec && ec != std::errc::no_such_file_or_directory) { error = "Cannot inspect path: " + ec.message(); return std::nullopt; }
        ec.clear();
    }
    target = fs::weakly_canonical(target, ec);
    if (ec || !Within(canonicalRoot, target) || target == canonicalRoot)
    { error = "Path escapes the project root"; return std::nullopt; }
    if (mustExist && !fs::exists(target, ec)) { error = "Project file is missing: " + ToUtf8(path); return std::nullopt; }
    return target;
}

bool jEditorProject::Create(const fs::path& directory, const std::string& name, std::string& error)
{
    error.clear();
    if (name.empty() || name.size() > 256) { error = "Project name must contain 1..256 bytes"; return false; }
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) { error = "Cannot create project directory: " + ec.message(); return false; }
    auto root = fs::canonical(directory, ec);
    if (ec || fs::exists(root / "project.jproject")) { error = "Project already exists or directory is invalid"; return false; }
    jEditorProject candidate;
    candidate.Root = root;
    candidate.ProjectFile = root / "project.jproject";
    candidate.Config.Name = name;
    candidate.Config.Id = NewId();
    fs::create_directories(root / "Assets", ec);
    if (!ec) fs::create_directories(root / "Scenes", ec);
    if (ec || !candidate.Save(error)) { if (error.empty()) error = ec.message(); return false; }
    *this = std::move(candidate);
    return true;
}

bool jEditorProject::Open(const fs::path& projectFile, std::string& error)
{
    Json json;
    if (!ReadJson(projectFile, json, error)) return false;
    try
    {
        if (json.at("schemaVersion").get<int>() != 1) throw std::runtime_error("Unsupported project schema");
        jEditorProject candidate;
        std::error_code ec;
        candidate.ProjectFile = fs::canonical(projectFile, ec);
        if (ec) throw std::runtime_error("Cannot resolve project file");
        candidate.Root = candidate.ProjectFile.parent_path();
        candidate.Config.Id = json.at("projectId").get<std::string>();
        candidate.Config.Name = json.at("name").get<std::string>();
        candidate.Config.AssetRoot = json.at("assetRoot").get<std::string>();
        candidate.Config.StartupScene = json.value("startupScene", std::string());
        candidate.Config.Settings = json.value("settings", std::map<std::string, std::string>{});
        if (!ValidId(candidate.Config.Id) || candidate.Config.Name.empty() || candidate.Config.Name.size() > 256)
            throw std::runtime_error("Invalid project identity");
        if (!ResolveSafe(candidate.Root, candidate.Config.AssetRoot, error)) return false;
        if (!candidate.Config.StartupScene.empty() && !ResolveSafe(candidate.Root, candidate.Config.StartupScene, error)) return false;
        *this = std::move(candidate); error.clear(); return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool jEditorProject::Save(std::string& error) const
{
    if (Root.empty() || !ValidId(Config.Id) || Config.Name.empty() || Config.Name.size() > 256)
    { error = "Invalid project configuration"; return false; }
    if (!ResolveSafe(Root, Config.AssetRoot, error)) return false;
    if (!Config.StartupScene.empty() && !ResolveSafe(Root, Config.StartupScene, error)) return false;
    return AtomicWrite(ProjectFile, {{"schemaVersion", 1}, {"projectId", Config.Id}, {"name", Config.Name},
        {"assetRoot", Config.AssetRoot}, {"startupScene", Config.StartupScene}, {"settings", Config.Settings}}, error);
}

std::vector<fs::path> jEditorProject::LoadRecent(const fs::path& recentFile, std::string& error)
{
    error.clear();
    std::error_code ec;
    if (!fs::exists(recentFile, ec)) return {};
    Json json; if (!ReadJson(recentFile, json, error)) return {};
    std::vector<fs::path> paths;
    try
    {
        if (json.at("schemaVersion").get<int>() != 1) throw std::runtime_error("Unsupported recent-project schema");
        const auto values = json.at("projects").get<std::vector<std::string>>();
        for (const auto& value : values)
            if (paths.size() < 16 && fs::is_regular_file(FromUtf8(value), ec)) paths.emplace_back(FromUtf8(value));
    }
    catch (const std::exception& exception) { error = exception.what(); paths.clear(); }
    return paths;
}

bool jEditorProject::RememberRecent(const fs::path& recentFile, std::string& error) const
{
    if (ProjectFile.empty()) { error = "No open project"; return false; }
    auto paths = LoadRecent(recentFile, error);
    if (!error.empty()) return false;
    paths.erase(std::remove(paths.begin(), paths.end(), ProjectFile), paths.end());
    paths.insert(paths.begin(), ProjectFile);
    if (paths.size() > 16) paths.resize(16);
    std::vector<std::string> strings; for (const auto& path : paths) strings.push_back(ToUtf8(path));
    return AtomicWrite(recentFile, {{"schemaVersion", 1}, {"projects", strings}}, error);
}

namespace
{
    struct Catalog
    {
        std::vector<jEditorAssetRecord> Assets;
        std::vector<jEditorAssetDiagnostic> Diagnostics;
        std::vector<std::pair<fs::path, Json>> Writes;
        std::string Error;
        std::uint64_t Generation = 0;
        bool Cancelled = false;
    };

    Catalog ReadCatalog(const fs::path& root, const std::string& assetRoot, std::uint64_t generation, const Cancel& cancel)
    {
        Catalog catalog; catalog.Generation = generation;
        std::map<std::string, size_t> paths;
        std::map<std::string, Json> previousMetadata;
        std::set<std::string> changed;
        const auto diagnostic = [&](const std::string& path, const std::string& message) {
            if (catalog.Diagnostics.size() < MaxDiagnostics) catalog.Diagnostics.push_back({path, message});
        };
        try
        {
            const auto add = [&](const fs::path& relative, bool missing) -> bool {
                if (cancel->load()) { catalog.Cancelled = true; return false; }
                const auto key = ToUtf8(relative);
                if (paths.count(key)) return true;
                if (catalog.Assets.size() >= MaxAssets) { catalog.Error = "Asset catalog exceeds 50000 files"; return false; }
                std::string pathError;
                const auto absolute = jEditorProject::ResolveSafe(root, relative, pathError, !missing);
                if (!absolute) { catalog.Error = key + ": " + pathError; return false; }
                jEditorAssetRecord asset; asset.SourcePath = key; asset.Kind = Kind(relative); asset.Missing = missing;
                const auto sidecar = Sidecar(*absolute);
                std::error_code ec;
                if (fs::exists(sidecar, ec))
                {
                    if (!ReadMetadata(sidecar, asset, catalog.Error)) return false;
                    previousMetadata[key] = Metadata(asset);
                }
                else asset.Id = NewId();
                if (!missing)
                {
                    jEditorAssetFingerprint fingerprint;
                    if (!Fingerprint(*absolute, fingerprint, catalog.Error, cancel))
                    { catalog.Cancelled = cancel->load(); return false; }
                    if (!asset.Fingerprint.Hash.empty()
                        && (asset.Fingerprint.Hash != fingerprint.Hash || asset.Fingerprint.Size != fingerprint.Size))
                    { ++asset.Revision; changed.insert(asset.Id); }
                    asset.Fingerprint = std::move(fingerprint);
                }
                paths.emplace(key, catalog.Assets.size());
                catalog.Assets.push_back(std::move(asset));
                return true;
            };

            std::vector<fs::path> roots;
            std::string pathError;
            const auto content = jEditorProject::ResolveSafe(root, assetRoot, pathError);
            if (!content) { catalog.Error = pathError; return catalog; }
            roots.push_back(*content);
            const auto scenes = root / "Scenes";
            if (!Within(*content, scenes)) roots.push_back(scenes);
            std::set<std::string> sourcePaths, metadataPaths;
            for (const auto& folder : roots)
            {
                std::error_code ec;
                if (!fs::exists(folder, ec)) continue;
                fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                while (it != end)
                {
                    if (cancel->load()) { catalog.Cancelled = true; return catalog; }
                    const auto entry = *it;
                    const auto relative = entry.path().lexically_relative(root);
                    const auto status = entry.symlink_status(ec);
                    if (ec) { diagnostic(ToUtf8(relative), ec.message()); ec.clear(); }
                    else if (fs::is_symlink(status) || Excluded(relative))
                    {
                        if (fs::is_directory(status) || fs::is_symlink(status)) it.disable_recursion_pending();
                        if (fs::is_symlink(status)) diagnostic(ToUtf8(relative), "Symlink excluded from catalog");
                    }
                    else if (fs::is_regular_file(status))
                    {
                        if (entry.path().extension() == ".jasset") metadataPaths.insert(ToUtf8(relative));
                        else if (ToUtf8(entry.path().filename()).find(".tmp-") == std::string::npos)
                            sourcePaths.insert(ToUtf8(relative));
                    }
                    it.increment(ec);
                    if (ec) { diagnostic(ToUtf8(folder), ec.message()); ec.clear(); }
                }
            }
            for (const auto& relative : sourcePaths) if (!add(FromUtf8(relative), false)) return catalog;
            for (auto relative : metadataPaths)
            {
                relative.resize(relative.size() - std::string(".jasset").size());
                if (!sourcePaths.count(relative) && !add(FromUtf8(relative), true)) return catalog;
            }

            std::vector<References> references;
            for (size_t index = 0; index < catalog.Assets.size(); ++index)
            {
                if (cancel->load()) { catalog.Cancelled = true; return catalog; }
                References refs;
                const auto source = catalog.Assets[index].SourcePath;
                if (!catalog.Assets[index].Missing && !SourceReferences(root, FromUtf8(source), refs, catalog.Error)) return catalog;
                for (const auto& path : refs.Paths)
                {
                    std::string resolveError;
                    const auto absolute = jEditorProject::ResolveSafe(root, path, resolveError);
                    if (!absolute) { catalog.Error = source + ": " + resolveError; return catalog; }
                    std::error_code ec;
                    if (!paths.count(path) && fs::is_regular_file(*absolute, ec) && !Excluded(FromUtf8(path)))
                        if (!add(FromUtf8(path), false)) return catalog;
                }
                references.push_back(std::move(refs));
            }
            std::map<std::string, size_t> ids;
            std::set<std::string> portablePaths;
            for (size_t i = 0; i < catalog.Assets.size(); ++i)
            {
                const auto& asset = catalog.Assets[i];
                if (!ids.emplace(asset.Id, i).second)
                { catalog.Error = "Duplicate persistent asset ID: " + asset.Id; return catalog; }
                if (!portablePaths.insert(Lower(asset.SourcePath)).second)
                { catalog.Error = "Asset paths collide on case-insensitive platforms: " + asset.SourcePath; return catalog; }
            }
            for (size_t i = 0; i < catalog.Assets.size(); ++i)
            {
                auto& asset = catalog.Assets[i];
                if (asset.Missing) { diagnostic(asset.SourcePath, "Source file is missing; persistent identity retained"); continue; }
                asset.Dependencies.clear(); asset.DependencyPaths.clear(); asset.MissingDependencies.clear();
                const auto addDependency = [&](const jEditorAssetRecord& dependency) {
                    asset.Dependencies.push_back(dependency.Id); asset.DependencyPaths.push_back(dependency.SourcePath);
                    if (dependency.Missing) asset.MissingDependencies.push_back(dependency.SourcePath);
                };
                for (const auto& path : references[i].Paths)
                {
                    const auto found = paths.find(path);
                    if (found != paths.end()) addDependency(catalog.Assets[found->second]);
                    else { asset.DependencyPaths.push_back(path); asset.MissingDependencies.push_back(path); }
                }
                for (const auto& id : references[i].Ids)
                {
                    const auto found = ids.find(id);
                    if (found != ids.end()) addDependency(catalog.Assets[found->second]);
                    else asset.MissingDependencies.push_back("assetId:" + id);
                }
                const auto unique = [](auto& values) {
                    std::sort(values.begin(), values.end()); values.erase(std::unique(values.begin(), values.end()), values.end());
                };
                unique(asset.Dependencies); unique(asset.DependencyPaths); unique(asset.MissingDependencies);
                for (const auto& missing : asset.MissingDependencies) diagnostic(asset.SourcePath, "Missing dependency: " + missing);
            }
            bool progressed = true;
            while (progressed)
            {
                progressed = false;
                for (auto& asset : catalog.Assets)
                    if (!changed.count(asset.Id) && std::any_of(asset.Dependencies.begin(), asset.Dependencies.end(), [&](const auto& id) { return changed.count(id); }))
                    { ++asset.Revision; changed.insert(asset.Id); progressed = true; }
            }
            for (const auto& asset : catalog.Assets)
            {
                const auto metadata = Metadata(asset);
                const auto previous = previousMetadata.find(asset.SourcePath);
                if (previous == previousMetadata.end() || previous->second != metadata)
                    catalog.Writes.emplace_back(Sidecar(root / FromUtf8(asset.SourcePath)), metadata);
            }
            std::sort(catalog.Assets.begin(), catalog.Assets.end(), [](const auto& a, const auto& b) { return a.SourcePath < b.SourcePath; });
        }
        catch (const std::exception& exception) { catalog.Error = exception.what(); }
        return catalog;
    }
}

struct jEditorAssetRegistry::Impl
{
    fs::path Root;
    std::string AssetRoot;
    std::thread::id Owner = std::this_thread::get_id();
    std::vector<jEditorAssetRecord> Assets;
    std::uint64_t Revision = 0, Generation = 0;
    std::future<Catalog> Work;
    Cancel Cancellation;
    bool Pending = false, Stopping = false;

    bool IsOwner() const { return Owner == std::this_thread::get_id(); }
    void Start()
    {
        Cancellation = std::make_shared<std::atomic<bool>>(false);
        const auto token = Cancellation;
        const auto root = Root;
        const auto assetRoot = AssetRoot;
        const auto generation = Generation;
        Work = std::async(std::launch::async, [root, assetRoot, generation, token] {
            return ReadCatalog(root, assetRoot, generation, token);
        });
        Pending = false;
    }
    bool Publish(Catalog& result, std::string& error)
    {
        if (result.Cancelled || !result.Error.empty()) { error = result.Cancelled ? "Cancelled" : result.Error; return false; }
        // Reject source changes while a worker snapshot was in flight before writing metadata.
        for (const auto& asset : result.Assets)
        {
            if (asset.Missing) continue;
            std::error_code ec;
            const auto size = fs::file_size(Root / FromUtf8(asset.SourcePath), ec);
            const auto modified = fs::last_write_time(Root / FromUtf8(asset.SourcePath), ec);
            if (ec || size != asset.Fingerprint.Size || static_cast<std::int64_t>(modified.time_since_epoch().count()) != asset.Fingerprint.Modified)
            { error = "Asset changed during scan; scan again: " + asset.SourcePath; return false; }
        }
        for (const auto& write : result.Writes)
            if (!AtomicWrite(write.first, write.second, error)) return false;
        Assets = std::move(result.Assets); ++Revision; error.clear(); return true;
    }
    bool Allowed(const std::string& relative) const
    {
        try
        {
            const auto path = FromUtf8(relative);
            return !Excluded(path) && (Within(FromUtf8(AssetRoot), path) || Within(FromUtf8("Scenes"), path));
        }
        catch (...) { return false; }
    }
    void Join()
    {
        if (Cancellation) Cancellation->store(true);
        if (Work.valid()) { Work.wait(); Work.get(); }
    }
};

jEditorAssetRegistry::jEditorAssetRegistry(const jEditorProject& project) : State(std::make_unique<Impl>())
{ State->Root = project.GetRoot(); State->AssetRoot = project.GetConfig().AssetRoot; }
jEditorAssetRegistry::~jEditorAssetRegistry() { Shutdown(); }

bool jEditorAssetRegistry::Scan(std::string& error)
{
    if (!State->IsOwner() || State->Stopping) { error = "Asset registry owner is unavailable"; return false; }
    CancelScan(); State->Join();
    const auto token = std::make_shared<std::atomic<bool>>(false);
    auto result = ReadCatalog(State->Root, State->AssetRoot, State->Generation, token);
    return State->Publish(result, error);
}

std::uint64_t jEditorAssetRegistry::ScanAsync()
{
    if (!State->IsOwner() || State->Stopping) return 0;
    if (State->Cancellation) State->Cancellation->store(true);
    ++State->Generation; State->Pending = true;
    if (!State->Work.valid()) State->Start();
    return State->Generation;
}

std::optional<jEditorAssetRegistry::ScanResult> jEditorAssetRegistry::PollScan()
{
    if (!State->IsOwner() || !State->Work.valid()
        || State->Work.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return std::nullopt;
    auto result = State->Work.get();
    ScanResult summary; summary.Generation = result.Generation; summary.Diagnostics = std::move(result.Diagnostics);
    if (State->Stopping || result.Generation != State->Generation || result.Cancelled)
        summary.Cancelled = true;
    else summary.Succeeded = State->Publish(result, summary.Error);
    summary.AssetCount = State->Assets.size();
    if (State->Pending && !State->Stopping) State->Start();
    return summary;
}

void jEditorAssetRegistry::CancelScan()
{
    if (!State->IsOwner()) return;
    if (State->Cancellation) State->Cancellation->store(true);
    State->Pending = false; ++State->Generation;
}

jEditorAssetRegistry::WorkStats jEditorAssetRegistry::GetWorkStats() const
{
    if (!State->IsOwner()) return {};
    const bool active = State->Work.valid();
    return {State->Generation, active, State->Pending,
        active && State->Work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready};
}

void jEditorAssetRegistry::Shutdown()
{
    State->Stopping = true; State->Pending = false; State->Join();
}

std::uint64_t jEditorAssetRegistry::GetRevision() const { return State->Revision; }
const std::vector<jEditorAssetRecord>& jEditorAssetRegistry::GetAssets() const { return State->Assets; }
const jEditorAssetRecord* jEditorAssetRegistry::FindById(const std::string& id) const
{
    const auto found = std::find_if(State->Assets.begin(), State->Assets.end(), [&](const auto& asset) { return asset.Id == id; });
    return found == State->Assets.end() ? nullptr : &*found;
}
const jEditorAssetRecord* jEditorAssetRegistry::FindBySourcePath(const std::string& path) const
{
    const auto found = std::find_if(State->Assets.begin(), State->Assets.end(), [&](const auto& asset) { return asset.SourcePath == path; });
    return found == State->Assets.end() ? nullptr : &*found;
}
std::optional<fs::path> jEditorAssetRegistry::Resolve(const std::string& id, std::string& error) const
{
    const auto* asset = FindById(id);
    if (!asset || asset->Missing) { error = "Missing asset ID: " + id; return std::nullopt; }
    return jEditorProject::ResolveSafe(State->Root, asset->SourcePath, error, true);
}

bool jEditorAssetRegistry::Reimport(const std::string& inputId, std::vector<std::string>& invalidated, std::string& error)
{
    const std::string id = inputId; // Scan replaces the catalog a caller may be borrowing from.
    invalidated.clear();
    if (!Scan(error)) return false;
    const auto* selected = FindById(id);
    if (!selected || selected->Missing) { error = "Cannot reimport a missing asset"; return false; }
    auto assets = State->Assets;
    std::set<std::string> dirty{id};
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const auto& asset : assets)
            if (!dirty.count(asset.Id) && std::any_of(asset.Dependencies.begin(), asset.Dependencies.end(), [&](const auto& dependency) { return dirty.count(dependency); }))
            { dirty.insert(asset.Id); changed = true; }
    }
    for (auto& asset : assets)
        if (dirty.count(asset.Id))
        {
            ++asset.Revision;
            if (!AtomicWrite(Sidecar(State->Root / FromUtf8(asset.SourcePath)), Metadata(asset), error)) return false;
            invalidated.push_back(asset.Id);
        }
    State->Assets = std::move(assets); ++State->Revision; return true;
}

bool jEditorAssetRegistry::SetImportSettings(const std::string& inputId, const std::string& json, std::string& error)
{
    const std::string id = inputId;
    if (!State->IsOwner() || State->Stopping) { error = "Asset registry owner is unavailable"; return false; }
    if (json.size() > MaxJsonBytes) { error = "Import settings exceed 16 MiB"; return false; }
    auto value = Json::parse(json, nullptr, false);
    if (value.is_discarded() || !value.is_object())
    { error = "Import settings must be a bounded JSON object"; return false; }
    CancelScan(); State->Join();
    auto* asset = const_cast<jEditorAssetRecord*>(FindById(id));
    if (!asset || asset->Missing) { error = "Cannot edit missing asset settings"; return false; }
    auto updated = *asset; updated.ImportSettingsJson = value.dump(); ++updated.Revision;
    if (!AtomicWrite(Sidecar(State->Root / FromUtf8(asset->SourcePath)), Metadata(updated), error)) return false;
    *asset = std::move(updated); ++State->Revision;
    std::vector<std::string> invalidated;
    return Reimport(id, invalidated, error);
}

bool jEditorAssetRegistry::CreateFolder(const std::string& path, std::string& error)
{
    if (!State->IsOwner() || State->Stopping || !State->Allowed(path)) { error = "Folder must be inside project Assets or Scenes"; return false; }
    const auto target = jEditorProject::ResolveSafe(State->Root, path, error);
    if (!target) return false;
    std::error_code ec; fs::create_directories(*target, ec);
    if (ec) { error = ec.message(); return false; }
    return true;
}

bool jEditorAssetRegistry::ImportFile(const fs::path& source, const std::string& destination, std::string& error)
{
    if (!State->IsOwner() || State->Stopping || !State->Allowed(destination)) { error = "Import destination must be inside project Assets or Scenes"; return false; }
    const auto target = jEditorProject::ResolveSafe(State->Root, destination, error);
    if (!target) return false;
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(source, ec)) || !fs::is_regular_file(source, ec) || Excluded(source.filename()))
    { error = "Import source must be a regular asset file"; return false; }
    if (fs::exists(*target, ec)) { error = "Import destination already exists"; return false; }
    fs::create_directories(target->parent_path(), ec);
    if (!ec) fs::copy_file(source, *target, fs::copy_options::none, ec);
    if (ec) { error = "Cannot import asset: " + ec.message(); return false; }
    return Scan(error);
}

bool jEditorAssetRegistry::MovePath(const std::string& inputSource, const std::string& inputDestination, std::string& error)
{
    const std::string source = inputSource, destination = inputDestination;
    if (!State->IsOwner() || State->Stopping || !State->Allowed(source) || !State->Allowed(destination))
    { error = "Moves must remain inside project Assets or Scenes"; return false; }
    if (!Scan(error)) return false;
    const auto from = jEditorProject::ResolveSafe(State->Root, source, error, true);
    const auto to = jEditorProject::ResolveSafe(State->Root, destination, error);
    if (!from || !to) return false;
    if (Within(*from, *to) || fs::exists(*to)) { error = "Move destination already exists or is inside source"; return false; }
    std::set<std::string> moving;
    for (const auto& asset : State->Assets) if (Within(FromUtf8(source), FromUtf8(asset.SourcePath))) moving.insert(asset.Id);
    for (const auto& asset : State->Assets)
        if (!moving.count(asset.Id))
            for (const auto& id : asset.Dependencies)
                if (moving.count(id)) { error = "Move would break a source dependency from " + asset.SourcePath; return false; }
    std::error_code ec;
    fs::create_directories(to->parent_path(), ec);
    if (!ec) fs::rename(*from, *to, ec);
    if (ec) { error = "Cannot move asset: " + ec.message(); return false; }
    if (fs::is_regular_file(*to, ec) && fs::exists(Sidecar(*from), ec))
    {
        fs::rename(Sidecar(*from), Sidecar(*to), ec);
        if (ec) { std::error_code rollback; fs::rename(*to, *from, rollback); error = "Cannot move asset metadata: " + ec.message(); return false; }
    }
    return Scan(error);
}

bool jEditorAssetRegistry::TrashPath(const std::string& inputSource, fs::path& recoverablePath, std::string& error)
{
    const std::string source = inputSource;
    recoverablePath.clear();
    if (!State->IsOwner() || State->Stopping || !State->Allowed(source)) { error = "Only project assets may be trashed"; return false; }
    if (!Scan(error)) return false;
    const auto from = jEditorProject::ResolveSafe(State->Root, source, error, true);
    if (!from) return false;
    std::set<std::string> removing;
    for (const auto& asset : State->Assets) if (Within(FromUtf8(source), FromUtf8(asset.SourcePath))) removing.insert(asset.Id);
    for (const auto& asset : State->Assets)
        if (!removing.count(asset.Id))
            for (const auto& id : asset.Dependencies)
                if (removing.count(id)) { error = "Asset is referenced by " + asset.SourcePath; return false; }
    const auto relative = FromUtf8(".jeditor") / "trash" / NewId() / FromUtf8(source);
    const auto target = jEditorProject::ResolveSafe(State->Root, relative, error);
    if (!target) return false;
    std::error_code ec;
    fs::create_directories(target->parent_path(), ec);
    if (!ec) fs::rename(*from, *target, ec);
    if (ec) { error = "Cannot move asset to recoverable trash: " + ec.message(); return false; }
    if (fs::is_regular_file(*target, ec) && fs::exists(Sidecar(*from), ec))
    {
        fs::rename(Sidecar(*from), Sidecar(*target), ec);
        if (ec) { std::error_code rollback; fs::rename(*target, *from, rollback); error = "Cannot preserve trashed metadata"; return false; }
    }
    recoverablePath = *target;
    return Scan(error);
}

bool PrepareEditorThumbnail(const fs::path& filename, jEditorThumbnail& thumbnail, std::string& error, std::uint32_t maxEdge)
{
    thumbnail = {}; error.clear();
    if (Lower(ToUtf8(filename.extension())) != ".png" || !maxEdge || maxEdge > 256)
    { error = "Thumbnail requires PNG input and 1..256 output edge"; return false; }
    // Open the native path once and validate/decode the same bounded snapshot.
    // Reopening a filename after stbi_info permits concurrent file replacement
    // to bypass the dimension check, and narrow fopen loses Windows Unicode.
    std::ifstream input(filename, std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos(-1);
    if (size <= 0 || size > std::streampos(64 * 1024 * 1024))
    { error = "Thumbnail source is missing, empty or exceeds 64 MiB"; return false; }
    std::vector<unsigned char> encoded(static_cast<size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    if (!input || static_cast<size_t>(input.gcount()) != encoded.size() || input.peek() != std::char_traits<char>::eof())
    { error = "Thumbnail source changed or could not be read completely"; return false; }
    int width = 0, height = 0, channels = 0;
    const int byteCount = static_cast<int>(encoded.size());
    if (!stbi_info_from_memory(encoded.data(), byteCount, &width, &height, &channels) || width <= 0 || height <= 0
        || width > 8192 || height > 8192 || std::uint64_t(width) * height > 16 * 1024 * 1024)
    { error = "Thumbnail source has invalid or excessive dimensions"; return false; }
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(stbi_load_from_memory(encoded.data(), byteCount, &width, &height, &channels, 4), stbi_image_free);
    if (!pixels) { error = "PNG thumbnail decode failed"; return false; }
    const double scale = std::min(1.0, double(maxEdge) / std::max(width, height));
    thumbnail.Width = std::max(1u, static_cast<unsigned>(width * scale));
    thumbnail.Height = std::max(1u, static_cast<unsigned>(height * scale));
    thumbnail.Rgba.resize(size_t(thumbnail.Width) * thumbnail.Height * 4);
    for (unsigned y = 0; y < thumbnail.Height; ++y)
        for (unsigned x = 0; x < thumbnail.Width; ++x)
        {
            const size_t src = (size_t(y) * height / thumbnail.Height * width + size_t(x) * width / thumbnail.Width) * 4;
            const size_t dst = (size_t(y) * thumbnail.Width + x) * 4;
            std::copy_n(pixels.get() + src, 4, thumbnail.Rgba.data() + dst);
        }
    return true;
}

bool jEditorPackager::Build(const jEditorPackageRequest& request, jEditorPackageReport& report, std::string& error)
{
    report = {}; error.clear();
    if (!request.Project || !request.Registry || request.Project->GetRoot().empty()
        || request.OutputDirectory.empty() || request.RuntimeExecutable.empty())
    { error = "Package requires an open project, registry, output and explicit runtime executable"; return false; }
    const auto& project = *request.Project;
    const auto& registry = *request.Registry;
    const auto root = project.GetRoot();
    const auto scene = request.StartupScene.empty() ? project.GetConfig().StartupScene : request.StartupScene;
    const auto sourceScene = jEditorProject::ResolveSafe(root, scene, error, true);
    if (!sourceScene || Lower(ToUtf8(sourceScene->extension())) != ".jscene")
    { if (error.empty()) error = "Package startup must be a project .jscene"; return false; }
    std::error_code ec;
    auto output = request.OutputDirectory.is_absolute() ? request.OutputDirectory : root / request.OutputDirectory;
    output = fs::weakly_canonical(output, ec);
    if (ec || fs::exists(output, ec)) { error = "Package output must be a new directory"; return false; }
    const auto staging = FromUtf8(ToUtf8(output) + ".stage-" + NewId());
    struct Cleanup
    {
        fs::path Path;
        ~Cleanup() { if (!Path.empty()) { std::error_code ignored; fs::remove_all(Path, ignored); } }
    } cleanup{staging};
    try
    {
        fs::create_directories(staging, ec);
        if (ec) { error = "Cannot create package staging: " + ec.message(); return false; }
        std::map<std::string, fs::path> files;
        std::map<std::string, std::string> portableNames;
        const auto addFile = [&](const fs::path& source, const std::string& destination) -> bool {
            std::string pathError;
            if (!jEditorProject::ResolveSafe(staging, destination, pathError)) { error = pathError; return false; }
            if (Excluded(FromUtf8(destination))) { error = "Excluded package path: " + destination; return false; }
            const auto status = fs::symlink_status(source, ec);
            if (ec || fs::is_symlink(status) || !fs::is_regular_file(status))
            { error = "Package input is missing or not a regular file: " + ToUtf8(source); return false; }
            const auto existing = files.find(destination);
            if (existing != files.end())
            {
                if (existing->second == source) return true;
                error = "Package destination collision: " + destination; return false;
            }
            if (!portableNames.emplace(Lower(destination), destination).second)
            { error = "Case-insensitive package path collision: " + destination; return false; }
            files.emplace(destination, source);
            return true;
        };
        std::set<std::string> selected;
        std::vector<std::string> pending;
        const auto includePath = [&](const std::string& relative) -> bool {
            const auto* asset = registry.FindBySourcePath(relative);
            if (!asset) { error = "Asset is absent from registry; scan before packaging: " + relative; return false; }
            pending.push_back(asset->Id); return true;
        };
        References references;
        if (!SourceReferences(root, FromUtf8(scene), references, error)) return false;
        for (const auto& path : references.Paths) if (!includePath(path)) return false;
        pending.insert(pending.end(), references.Ids.begin(), references.Ids.end());
        if (const auto* sceneAsset = registry.FindBySourcePath(scene)) pending.push_back(sceneAsset->Id);
        while (!pending.empty())
        {
            const auto id = std::move(pending.back()); pending.pop_back();
            if (!selected.insert(id).second) continue;
            const auto* asset = registry.FindById(id);
            if (!asset || asset->Missing) { error = "Package dependency is missing: " + id; return false; }
            if (!asset->MissingDependencies.empty())
            { error = "Missing dependency of " + asset->SourcePath + ": " + asset->MissingDependencies.front(); return false; }
            const auto source = jEditorProject::ResolveSafe(root, asset->SourcePath, error, true);
            if (!source) return false;
            jEditorAssetFingerprint fingerprint;
            if (!Fingerprint(*source, fingerprint, error)) return false;
            if (fingerprint.Hash != asset->Fingerprint.Hash || fingerprint.Size != asset->Fingerprint.Size)
            { error = "Asset changed since scan; reimport before packaging: " + asset->SourcePath; return false; }
            if (!addFile(*source, asset->SourcePath) || !addFile(Sidecar(*source), asset->SourcePath + ".jasset")) return false;
            pending.insert(pending.end(), asset->Dependencies.begin(), asset->Dependencies.end());
        }
        if (!addFile(*sourceScene, scene)) return false;
        for (const auto& mapping : request.RuntimeFiles)
        {
            const auto status = fs::symlink_status(mapping.Source, ec);
            if (ec || fs::is_symlink(status)) { error = "Invalid or symlink runtime source: " + ToUtf8(mapping.Source); return false; }
            if (fs::is_regular_file(status))
            { if (!addFile(mapping.Source, mapping.Destination)) return false; }
            else if (fs::is_directory(status))
            {
                const auto runtimeRoot = fs::canonical(mapping.Source, ec);
                if (ec || Within(runtimeRoot, staging) || Within(runtimeRoot, output))
                { error = "Package output cannot be inside a runtime input tree"; return false; }
                fs::recursive_directory_iterator it(runtimeRoot, ec), end;
                while (!ec && it != end)
                {
                    const auto relative = it->path().lexically_relative(runtimeRoot);
                    const auto entryStatus = it->symlink_status(ec);
                    if (ec) break;
                    if (Excluded(relative)) { if (fs::is_directory(entryStatus)) it.disable_recursion_pending(); }
                    else if (fs::is_symlink(entryStatus)) { error = "Runtime input contains a symlink: " + ToUtf8(it->path()); return false; }
                    else if (fs::is_regular_file(entryStatus))
                    {
                        const auto destination = ToUtf8((FromUtf8(mapping.Destination) / relative));
                        if (!addFile(it->path(), destination)) return false;
                    }
                    it.increment(ec);
                }
                if (ec) { error = "Cannot enumerate runtime dependencies: " + ec.message(); return false; }
            }
            else { error = "Missing runtime dependency: " + ToUtf8(mapping.Source); return false; }
        }
        if (!files.count(request.RuntimeExecutable)) { error = "Explicit runtime executable was not included"; return false; }
        if (files.count("project.jproject") || files.count("jengine-runtime.json"))
        { error = "Runtime mapping collides with package manifest"; return false; }
        for (const auto& file : files)
        {
            const auto destination = jEditorProject::ResolveSafe(staging, file.first, error);
            if (!destination) return false;
            fs::create_directories(destination->parent_path(), ec);
            if (!ec) fs::copy_file(file.second, *destination, fs::copy_options::none, ec);
            if (ec) { error = "Package copy failed: " + file.first + ": " + ec.message(); return false; }
            jEditorAssetFingerprint original, copied;
            if (!Fingerprint(file.second, original, error) || !Fingerprint(*destination, copied, error)) return false;
            if (original.Hash != copied.Hash || original.Size != copied.Size)
            { error = "Package input changed while copying: " + file.first; return false; }
        }
        Json descriptor;
        if (request.FinalizeStaged)
        {
            if (!request.FinalizeStaged(staging, error)) return false;
            // Runtime fixups may add a launcher/ICD. Hash every generated file
            // after fixup, and reject links before publishing the package.
            for (const auto& entry : fs::recursive_directory_iterator(staging))
            {
                const auto status = entry.symlink_status();
                if (fs::is_symlink(status)) { error = "Runtime finalizer created a symlink"; return false; }
                if (fs::is_regular_file(status))
                {
                    const auto relative = ToUtf8(entry.path().lexically_relative(staging));
                    if (Excluded(FromUtf8(relative))) { error = "Runtime finalizer created an excluded file: " + relative; return false; }
                    files.emplace(relative, entry.path());
                }
            }
        }
        if (!ReadJson(project.GetProjectFile(), descriptor, error)) return false;
        descriptor["startupScene"] = scene;
        if (!AtomicWrite(staging / "project.jproject", descriptor, error)) return false;
        Json manifest{{"schemaVersion", 1}, {"project", "project.jproject"}, {"scene", scene},
            {"runtime", request.RuntimeExecutable}, {"projectId", project.GetConfig().Id},
            {"files", Json::array()}, {"assets", Json::array()}};
        files.emplace("project.jproject", staging / "project.jproject");
        for (const auto& file : files)
        {
            jEditorAssetFingerprint fingerprint;
            if (!Fingerprint(staging / FromUtf8(file.first), fingerprint, error)) return false;
            manifest["files"].push_back({{"path", file.first}, {"size", fingerprint.Size}, {"hash", fingerprint.Hash}});
            report.Bytes += fingerprint.Size;
        }
        for (const auto& id : selected)
        {
            const auto* asset = registry.FindById(id);
            manifest["assets"].push_back({{"id", id}, {"source", asset->SourcePath}, {"revision", asset->Revision}, {"dependencies", asset->Dependencies}});
        }
        if (!AtomicWrite(staging / "jengine-runtime.json", manifest, error)
            || !Validate(staging / "jengine-runtime.json", error)) return false;
        fs::rename(staging, output, ec);
        if (ec) { error = "Cannot publish completed package: " + ec.message(); return false; }
        cleanup.Path.clear();
        report.Manifest = output / "jengine-runtime.json";
        report.AssetCount = selected.size(); report.FileCount = files.size();
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool jEditorPackager::Validate(const fs::path& manifestPath, std::string& error)
{
    error.clear();
    try
    {
        std::error_code ec;
        if (fs::is_symlink(fs::symlink_status(manifestPath, ec))) { error = "Runtime manifest cannot be a symlink"; return false; }
        Json manifest; if (!ReadJson(manifestPath, manifest, error)) return false;
        if (manifest.at("schemaVersion").get<int>() != 1) { error = "Unsupported runtime package schema"; return false; }
        const auto root = fs::canonical(manifestPath.parent_path(), ec);
        if (ec) { error = "Invalid runtime package root"; return false; }
        const auto& files = manifest.at("files");
        if (!files.is_array() || files.size() > MaxAssets * 4) { error = "Invalid runtime file table"; return false; }
        std::set<std::string> names, portable;
        for (const auto& file : files)
        {
            const auto relative = file.at("path").get<std::string>();
            if (Excluded(FromUtf8(relative)) || !names.insert(relative).second || !portable.insert(Lower(relative)).second)
            { error = "Excluded or duplicate runtime file: " + relative; return false; }
            const auto absolute = jEditorProject::ResolveSafe(root, relative, error, true);
            if (!absolute) return false;
            jEditorAssetFingerprint fingerprint;
            if (!Fingerprint(*absolute, fingerprint, error)) return false;
            if (fingerprint.Size != file.at("size").get<std::uintmax_t>() || fingerprint.Hash != file.at("hash").get<std::string>())
            { error = "Runtime package integrity mismatch: " + relative; return false; }
        }
        for (const char* key : {"project", "scene", "runtime"})
        {
            const auto path = manifest.at(key).get<std::string>();
            if (!names.count(path) || !jEditorProject::ResolveSafe(root, path, error, true))
            { if (error.empty()) error = std::string("Missing runtime entry: ") + key; return false; }
        }
        jEditorProject project;
        if (!project.Open(root / FromUtf8(manifest.at("project").get<std::string>()), error)) return false;
        if (project.GetConfig().Id != manifest.at("projectId").get<std::string>())
        { error = "Runtime project identity mismatch"; return false; }
        const auto& assets = manifest.at("assets");
        if (!assets.is_array() || assets.size() > MaxAssets) { error = "Invalid runtime asset table"; return false; }
        std::set<std::string> ids;
        for (const auto& asset : assets)
            if (!ValidId(asset.at("id").get<std::string>()) || !ids.insert(asset.at("id").get<std::string>()).second)
            { error = "Invalid or duplicate packaged asset identity"; return false; }
        for (const auto& asset : assets)
        {
            const auto source = asset.at("source").get<std::string>();
            if (!names.count(source) || !names.count(source + ".jasset")) { error = "Packaged asset source or metadata missing"; return false; }
            jEditorAssetRecord metadata;
            if (!ReadMetadata(root / FromUtf8(source + ".jasset"), metadata, error)) return false;
            if (metadata.Id != asset.at("id").get<std::string>() || metadata.Revision != asset.at("revision").get<std::uint64_t>())
            { error = "Packaged asset identity/revision mismatch"; return false; }
            for (const auto& dependency : asset.at("dependencies").get<std::vector<std::string>>())
                if (!ids.count(dependency)) { error = "Packaged transitive asset dependency missing"; return false; }
        }
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}

std::optional<fs::path> jEditorPackager::ResolveStartupScene(const fs::path& manifest, std::string& error)
{
    if (!Validate(manifest, error)) return std::nullopt;
    Json json; if (!ReadJson(manifest, json, error)) return std::nullopt;
    return jEditorProject::ResolveSafe(manifest.parent_path(), json.at("scene").get<std::string>(), error, true);
}
