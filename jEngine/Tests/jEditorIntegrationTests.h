#pragma once

// Call after real RHI/engine initialization and initial scene imports complete.
// Runs bounded owner-thread Update/Draw/EndOfFrame cycles; writes a durable JSON
// result and fixture project beneath outputDirectory. No separate RHI is created.
int RunEditorAuthoringGpuTests(const char* outputDirectory);
// Root calls this once inside the real active ImGui authoring frame. Inactive
// outside the integration test; creates a real detached-window render probe.
void RenderEditorIntegrationProbe();
