#pragma once

// Requires the normal initialized engine/RHI/editor and a ready startup scene.
// The caller owns the process/GPU lane, native validation collection and INI /
// workspace-state isolation. Writes only beneath its explicit owned directory.
int RunAdvancedShadowIntegrationGpuTests(const char* ownedOutputDirectory);
