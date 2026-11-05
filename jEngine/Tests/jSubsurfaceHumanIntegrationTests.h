#pragma once

// Uses the initialized editor/importer/renderer and the caller's owned output
// directory. The caller owns preference isolation and the sole native GPU lane.
int RunSubsurfaceHumanIntegrationGpuTests(const char* ownedOutputDirectory);
