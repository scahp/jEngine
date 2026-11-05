#pragma once

#include <string>
#include <memory>
struct jTexture;

// Serialized actual preview readback metrics plus an unmodified PNG capture.
std::string CaptureSceneFeaturePixels(const char* pngPath);
std::string CaptureSceneFeatureTexturePixels(const std::shared_ptr<jTexture>& texture, const char* pngPath);

// Call after real RHI/engine initialization and initial scene imports complete.
// Runs bounded owner-thread Update/Draw/EndOfFrame cycles; writes a durable JSON
// result and fixture project beneath outputDirectory. No separate RHI is created.
int RunEditorAuthoringGpuTests(const char* outputDirectory);
int RunRendererSwitchGpuTests(const char* outputDirectory);
// Root calls this once inside the real active ImGui authoring frame. Inactive
// outside the integration test; creates a real detached-window render probe.
void RenderEditorIntegrationProbe();
