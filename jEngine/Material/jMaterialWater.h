#pragma once

#include "Renderer/Features/jWaterSimulation.h"

struct jMaterialGraphUniformBuffer;

// An authored water instance owns its simulation parameters and seconds clock.
// The renderer copies a snapshot into its frame material buffer; no GPU object
// or global time source is hidden in the simulation module.
struct jMaterialWaterState
{
    jWaterSimulation::Settings Settings;
    double TimeSeconds = 0.0;
    bool Enabled = true;
};

void FillMaterialWaterShaderParameters(const jMaterialWaterState* water,
    jMaterialGraphUniformBuffer& parameters);
