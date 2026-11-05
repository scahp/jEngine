#pragma once

#include "jEditorDocument.h"
#include <cmath>
#include <functional>

class jEditor;

namespace jEditorWater
{
inline constexpr std::size_t MaximumGpuWaves = 4;

// CPU-only canonical authoring validation; usable by document loading, tools and
// editor widgets without depending on ImGui, RHI or the editor runtime.
inline bool Validate(const jEditorAuthoring::WaterProperties& water, std::string& error)
{
    error.clear();
    if (water.Resolution < 2 || water.Resolution > 129 || !std::isfinite(water.SizeX)
        || !std::isfinite(water.SizeZ) || water.SizeX < 1e-4f || water.SizeX > 1e8f
        || water.SizeZ < 1e-4f || water.SizeZ > 1e8f || water.Waves.Waves.size() > MaximumGpuWaves)
    {
        error = "Water requires a 2..129 grid, finite positive sizes and at most four rendered waves.";
        return false;
    }
    return jWaterSimulation::Validate(water.Waves, error);
}

using PropertyEdit = std::function<void(jEditorAuthoring::Entity&, const char* label, bool changed)>;
// Called by the owner's inspector. The callback uses its existing property edit
// transaction/lifetime flow; this module never retains a document/entity pointer.
void RenderInspector(jEditor& editor, jEditorAuthoring::EntityId id, const PropertyEdit& apply);
}
