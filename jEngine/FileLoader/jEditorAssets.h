#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "jEditorAssetPath.h"

enum class jEditorAssetKind { Model, Texture, Material, Scene, Shader, Data };
const char* jEditorAssetKindName(jEditorAssetKind kind);

struct jEditorAssetFingerprint
{
    std::uintmax_t Size = 0;
    std::int64_t Modified = 0;
    std::string Hash;
    bool operator==(const jEditorAssetFingerprint&) const = default;
};

struct jEditorAssetRecord
{
    std::string Id; // Persistent UUID in <source>.jasset; independent of name/content/index.
    std::string SourcePath; // Project-relative generic path, including asset directory.
    jEditorAssetKind Kind = jEditorAssetKind::Data;
    jEditorAssetFingerprint Fingerprint;
    std::uint64_t Revision = 1;
    std::string ImportSettingsJson = "{}";
    std::vector<std::string> Dependencies; // Stable asset IDs.
    std::vector<std::string> DependencyPaths; // Project-relative source paths.
    std::vector<std::string> MissingDependencies;
    bool Missing = false;
};

struct jEditorAssetDiagnostic
{
    std::string Path;
    std::string Message;
};

struct jEditorProjectConfig
{
    std::string Id;
    std::string Name;
    std::string AssetRoot = "Assets";
    std::string StartupScene;
    std::map<std::string, std::string> Settings;
};

class jEditorProject
{
public:
    bool Create(const std::filesystem::path& directory, const std::string& name, std::string& error);
    bool Open(const std::filesystem::path& projectFile, std::string& error);
    bool Save(std::string& error) const;
    const std::filesystem::path& GetRoot() const { return Root; }
    const std::filesystem::path& GetProjectFile() const { return ProjectFile; }
    const jEditorProjectConfig& GetConfig() const { return Config; }
    jEditorProjectConfig& EditConfig() { return Config; }
    // Rejects absolute paths, traversal, symlink escapes and invalid portable segments.
    static std::optional<std::filesystem::path> ResolveSafe(const std::filesystem::path& root,
        const std::filesystem::path& relative, std::string& error, bool mustExist = false);
    static std::optional<std::filesystem::path> ResolveSafe(const std::filesystem::path& root,
        const std::string& relative, std::string& error, bool mustExist = false)
    {
        try { return ResolveSafe(root, jEditorAssetPath::FromUtf8(relative), error, mustExist); }
        catch (const std::exception& exception) { error = exception.what(); return std::nullopt; }
    }
    static std::optional<std::filesystem::path> ResolveSafe(const std::filesystem::path& root,
        const char* relative, std::string& error, bool mustExist = false)
    { return ResolveSafe(root, std::string(relative ? relative : ""), error, mustExist); }
    // The caller chooses an application-state file; no implicit HOME/cwd storage.
    bool RememberRecent(const std::filesystem::path& recentFile, std::string& error) const;
    static std::vector<std::filesystem::path> LoadRecent(const std::filesystem::path& recentFile,
        std::string& error);

private:
    std::filesystem::path Root;
    std::filesystem::path ProjectFile;
    jEditorProjectConfig Config;
};

class jEditorAssetRegistry
{
public:
    struct ScanResult
    {
        std::uint64_t Generation = 0;
        bool Succeeded = false;
        bool Cancelled = false;
        std::string Error;
        size_t AssetCount = 0;
        std::vector<jEditorAssetDiagnostic> Diagnostics;
    };
    struct WorkStats
    {
        std::uint64_t Generation = 0;
        bool Active = false;
        bool Pending = false;
        bool Ready = false;
    };
    explicit jEditorAssetRegistry(const jEditorProject& project);
    ~jEditorAssetRegistry();
    jEditorAssetRegistry(const jEditorAssetRegistry&) = delete;
    jEditorAssetRegistry& operator=(const jEditorAssetRegistry&) = delete;

    bool Scan(std::string& error);
    // Latest scan wins. One worker, one pending request, one completion snapshot.
    std::uint64_t ScanAsync();
    std::optional<ScanResult> PollScan(); // Publishes metadata/catalog on owner thread.
    void CancelScan();
    WorkStats GetWorkStats() const;
    void Shutdown();
    std::uint64_t GetRevision() const;
    const std::vector<jEditorAssetRecord>& GetAssets() const;
    const jEditorAssetRecord* FindById(const std::string& id) const;
    const jEditorAssetRecord* FindBySourcePath(const std::string& projectRelativePath) const;
    std::optional<std::filesystem::path> Resolve(const std::string& id, std::string& error) const;

    bool SetImportSettings(const std::string& id, const std::string& json, std::string& error);
    // Reports this asset plus transitive dependents. GPU/decoded cache refresh is
    // the runtime owner's responsibility using these new revision identities.
    bool Reimport(const std::string& id, std::vector<std::string>& invalidatedIds, std::string& error);
    bool ImportFile(const std::filesystem::path& externalSource, const std::string& destination,
        std::string& error);
    bool CreateFolder(const std::string& projectRelativePath, std::string& error);
    // Moves source and sidecar together. Rejects changes that break inbound source dependencies.
    bool MovePath(const std::string& source, const std::string& destination, std::string& error);
    // Recoverable project-local trash, never permanent deletion.
    bool TrashPath(const std::string& source, std::filesystem::path& recoverablePath, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> State;
};

struct jEditorThumbnail
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<std::uint8_t> Rgba;
};
// CPU-only image thumbnail, bounded source dimensions and <=256x256 output.
// The caller may schedule this on its worker and upload the result on the owner.
bool SupportsEditorThumbnail(const std::filesystem::path& filename);
bool PrepareEditorThumbnail(const std::filesystem::path& filename, jEditorThumbnail& thumbnail,
    std::string& error, std::uint32_t maxEdge = 128);

struct jEditorRuntimeFile
{
    std::filesystem::path Source; // Explicit engine binary, DXC, libraries, shader/resource tree.
    std::string Destination; // Package-relative path; no executable/source-tree assumptions.
};

struct jEditorPackageRequest
{
    const jEditorProject* Project = nullptr;
    const jEditorAssetRegistry* Registry = nullptr;
    std::filesystem::path OutputDirectory;
    std::string StartupScene; // Project-relative scene file.
    std::string RuntimeExecutable; // Explicit package-relative executable entry.
    std::vector<jEditorRuntimeFile> RuntimeFiles;
    // Runs only on the fresh staging tree, before hashes/manifest publication.
    std::function<bool(const std::filesystem::path&, std::string&)> FinalizeStaged;
};

struct jEditorPackageReport
{
    std::filesystem::path Manifest;
    size_t AssetCount = 0;
    size_t FileCount = 0;
    std::uintmax_t Bytes = 0;
};

class jEditorPackager
{
public:
    // A new output directory is published only after the staged copy validates.
    // Includes scene + transitive registry dependencies, project/asset metadata,
    // explicit runtime inputs. Never follows symlinks or copies a source tree implicitly.
    static bool Build(const jEditorPackageRequest& request, jEditorPackageReport& report, std::string& error);
    // Resolves paths relative to the manifest location and verifies every file fingerprint.
    static bool Validate(const std::filesystem::path& manifest, std::string& error);
    static std::optional<std::filesystem::path> ResolveStartupScene(
        const std::filesystem::path& manifest, std::string& error);
};
