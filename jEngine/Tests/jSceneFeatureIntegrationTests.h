#pragma once

// Requires the normal initialized engine/RHI/editor. The caller owns preference
// isolation, validation-message collection and the process/GPU execution lane.
int RunSceneFeatureIntegrationGpuTests(const char* outputDirectory);
