#pragma once

// Normal initialized engine/editor/RHI required. Caller owns strict CLI/output
// validation, preference isolation, validation logs and the sole native GPU lane.
int RunSubsurfaceIntegrationGpuTests(const char* ownedOutputDirectory);
