#pragma once

#include "Features/jSkinShadingSettings.h"
#include <cstddef>
#include <memory>

struct jTexture;
struct jRenderFrameContext;
namespace jSkinShading
{
struct FrameStats
{
    bool Ready=false,GeneratedThisCall=false;
    std::uint32_t Resolution=EnergyLUTResolution,QuadratureSteps=IntegrationSteps,Dispatches=0;
    std::size_t AdditionalTextureBytes=0;
    std::string Diagnostic;
};
// Owner-thread preflight, before light setup workers. Records one bounded
// compute pass on the existing graphics command buffer, then a write->read
// barrier. No submission, queue wait, CPU bake/readback or per-material texture.
// The immutable R32F lease is shared until ReleaseResources after the GPU flush.
std::shared_ptr<jTexture> AcquireEnergyLUT(const std::shared_ptr<jRenderFrameContext>& frame);
const FrameStats& GetLastStats();
void ReleaseResources();
}
