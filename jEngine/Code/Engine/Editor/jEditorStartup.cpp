#include "pch.h"
#include "jEditorStartup.h"
#include "jEditorWorkspace.h"
#include "Code/Engine/jEditor.h"
#include "FileLoader/jEditorAssets.h"
#include "FileLoader/jModelUploadSession.h"
#include "External/PathTracingDataLoader/json.hpp"
#include "jEngine.h"
#include <fstream>
#include <thread>
namespace { std::filesystem::path StartupProject, StartupScene; bool Runtime = false; }
bool HasEditorStartupScene() { return !StartupProject.empty() || !StartupScene.empty(); }
bool ConfigureEditorStartup(int argc, char** argv, std::string& error)
{
    try
    {
        StartupProject.clear(); StartupScene.clear(); Runtime = false;
        if (argc && argv[0]) SetAuthoringExecutablePath(std::filesystem::absolute(jEditorAssetPath::FromUtf8(argv[0])));
        std::filesystem::path manifest;
        for (int i = 1; i < argc; ++i)
        {
            const std::string argument(argv[i]);
            if (argument != "--project" && argument != "--scene" && argument != "--runtime") continue;
            if (++i == argc || !argv[i] || !argv[i][0]) { error = argument + " requires a file path"; return false; }
            auto& target = argument == "--project" ? StartupProject : argument == "--scene" ? StartupScene : manifest;
            if (!target.empty()) { error = "Duplicate " + argument; return false; }
            target = std::filesystem::absolute(jEditorAssetPath::FromUtf8(argv[i]));
        }
        if (!manifest.empty())
        {
            if (!StartupProject.empty() || !StartupScene.empty()) { error = "--runtime cannot be combined with --project or --scene"; return false; }
            if (!jEditorPackager::Validate(manifest, error)) return false;
            std::ifstream input(manifest); nlohmann::json value; input >> value;
            const auto root = std::filesystem::weakly_canonical(manifest).parent_path();
            const auto project = jEditorProject::ResolveSafe(root, value.at("project").get<std::string>(), error, true);
            const auto scene = jEditorPackager::ResolveStartupScene(manifest, error);
            if (!project || !scene) return false;
            StartupProject = *project; StartupScene = *scene; Runtime = true;
            std::filesystem::current_path(root);
#ifdef __APPLE__
            const auto dxc = root / "runtime/dxc/bin/dxc";
            if (std::filesystem::is_regular_file(dxc)) setenv("JENGINE_DXC_EXECUTABLE", dxc.c_str(), 1);
            const auto icd = root / "runtime/MoltenVK_icd.json";
            if (std::filesystem::is_regular_file(icd)) setenv("VK_DRIVER_FILES", icd.c_str(), 1);
#endif
        }
        if (!StartupProject.empty())
        {
            jEditorProject project; if (!project.Open(StartupProject, error)) return false;
            if (StartupScene.empty() && !project.GetConfig().StartupScene.empty())
            {
                const auto scene = jEditorProject::ResolveSafe(project.GetRoot(), project.GetConfig().StartupScene, error, true);
                if (!scene) return false; StartupScene = *scene;
            }
        }
        if (!StartupScene.empty()) { jEditorAuthoring::Document candidate; if (!candidate.Load(StartupScene)) { error = candidate.LastError(); return false; } }
        SetAuthoringRuntimeMode(Runtime); return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
bool InitializeEditorStartup(jEditor& editor, std::string& error)
{
    if (!StartupProject.empty() && !OpenAuthoringProject(editor, StartupProject, error)) return false;
    if (!StartupScene.empty() && editor.GetDocument().Path() != StartupScene && !LoadAuthoringScene(editor, StartupScene, error)) return false;
    if (HasEditorStartupScene()) { editor.SetAuthoringEnabled(true); if (!editor.SyncDocumentToWorld()) { error = editor.LastError(); return false; } }
    if (Runtime && !editor.BeginPlay()) { error = editor.LastError(); return false; }
    return true;
}
bool WaitForEditorImports(jEditor& editor, unsigned timeoutMilliseconds, std::string& error)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    do
    {
        jModelUploadSession::CollectRetired(); editor.Tick(0, g_Engine ? g_Engine->Game.MainCamera : nullptr);
        if (!editor.PendingImportCount())
        {
            for (const auto& [id, entity] : editor.GetDocument().Entities())
                if (entity.Kind == jEditorAuthoring::EntityKind::Model && !entity.SourceAsset.empty()
                    && editor.GetDocument().IsEffectivelyVisible(id) && !editor.ObjectForEntity(id))
                { error = editor.LastError().empty() ? "Authored model did not publish: " + entity.SourceAsset : editor.LastError(); return false; }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    error = "Timed out waiting for authored scene CPU/GPU publication"; return false;
}
