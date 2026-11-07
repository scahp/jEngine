#pragma once
#include "Renderer/Experiments/jRenderingExperiment.h"
#include <filesystem>
#include <memory>
#include <string>

class jEditor;
struct jRenderFrameContext;
struct jTexture;

namespace jRenderingLab
{
struct Status
{
    bool Running = false;
    std::string Phase = "Idle", Error;
    size_t Task = 0, TaskCount = 0;
    uint32_t Frame = 0, FrameCount = 0;
    std::filesystem::path Directory;
};
bool IsVisible();
void SetVisible(bool visible);
bool IsRunning();
const Status& GetStatus();
// Queues a bounded owner-thread transaction; no GPU calls from a UI callback.
bool Start(const jRenderingExperiment::Definition& definition,
    const std::filesystem::path& outputRoot, std::string& error);
void Cancel();
void BeforeFrame();
bool ControlledUpdate();
float CameraAspect();
void AfterFrame();
void CaptureHDR(const std::shared_ptr<jRenderFrameContext>& frame, jTexture& source);
void Render(jEditor& editor);
void Shutdown();
int RunFromCommandLine(const char* definitionFile, const char* outputRoot);
int ExportShaderFixtures(const char* directory);
}
