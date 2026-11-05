#pragma once

#include "Renderer/Features/jHorizonMapping.h"
#include <cstdint>
#include <string>
#include <vector>

class jEditor;
class jEditorProject;

namespace jSceneFeatureTools
{
enum class BakeKind : std::uint32_t { EnvironmentSH = 0, Horizon = 1 };
struct BakeRequest
{
    BakeKind Kind = BakeKind::EnvironmentSH;
    // Existing project-relative source and explicit project-relative destination.
    // SH destination is a .jsh file; Horizon destination is a managed bake folder.
    std::string SourcePath, OutputPath;
    std::uint32_t HorizonResolution = 128, RadiusTexels = 16;
    float HeightScale = 1.f, TexelSize = 1.f;
    jHorizonMapping::Boundary Edges = jHorizonMapping::Boundary::Wrap;
    bool UseEnvironmentAfterBake = true;
};
struct BakeStatus
{
    bool Running = false, Saving = false, Succeeded = false, Cancelled = false;
    std::string Message;
    std::vector<std::string> Outputs;
};

void SetVisible(bool visible);
bool IsVisible();
void Render(jEditor& editor, jEditorProject* project);
// Owner-thread completion publication/invalidation, before rendering the frame.
void Tick(jEditor& editor, jEditorProject* project);
// Cancels and drains the single CPU worker before editor/loader shutdown.
void Shutdown();
bool StartBake(const jEditorProject& project, const BakeRequest& request, std::string& error);
void CancelBake();
BakeStatus GetBakeStatus();
}
