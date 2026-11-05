#pragma once

// Standalone pure CPU contracts. No RHI, device, application frame or settings
// mutation is needed; root may wire this into the existing rendering CPU group.
int RunSubsurfaceDiffusionTests();
