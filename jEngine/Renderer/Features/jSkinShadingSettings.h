#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace jSkinShading
{
enum class Model : std::uint32_t { StandardPBR=0, SkinKelemen=1 };
inline constexpr std::uint32_t EnergyLUTResolution=64;
inline constexpr std::uint32_t IntegrationSteps=80;
inline constexpr double MinimumRoughness=0.02;
inline constexpr double MaximumRoughness=1.0;
inline constexpr double MaximumSpecularWeight=8.0;
inline constexpr double FresnelF0=0.02777778;
struct Settings
{
    Model ShadingModel=Model::StandardPBR;
    // Kelemen rho_s, not dielectric F0. Existing surface roughness is Beckmann
    // m only for SkinKelemen; StandardPBR keeps its original GGX meaning.
    float SpecularWeight=1.f;
    bool operator==(const Settings&)const=default;
};
inline bool ValidateSettings(const Settings& input,Settings& output,std::string& error)
{
    error.clear();
    if((input.ShadingModel!=Model::StandardPBR&&input.ShadingModel!=Model::SkinKelemen)||
        !std::isfinite(input.SpecularWeight)||input.SpecularWeight<0.f||input.SpecularWeight>float(MaximumSpecularWeight))
    {error="Skin shading requires StandardPBR or SkinKelemen and finite specular weight [0,8].";return false;}
    output=input;return true;
}
}
