#pragma once

#include "jSkinShadingSettings.h"
#include <array>

namespace jSkinShading
{
using Vector3=std::array<double,3>;
// Pure bounded references for scahp/Shadows Skin51249d. All invalid inputs
// leave output unchanged; degenerate finite directions have zero specular.
bool BeckmannPH(double normalDotHalf,double roughness,double& output,std::string& error);
bool KelemenSpecular(const Vector3& normal,const Vector3& toLight,const Vector3& toEye,
    double roughness,double specularWeight,double& output,std::string& error);
// Source rectangle quadrature: 80 endpoint-inclusive theta/phi samples,
// weights (PI/2)/80 and 2PI/80. rho_s=1; input is cos(theta_view).
bool IntegrateSpecularEnergy(double normalDotView,double roughness,double& output,std::string& error);
// Same mapping as the GPU build, including its bounded roughness domain.
bool EnergyTexel(std::uint32_t x,std::uint32_t y,double& output,std::string& error);
double DiffuseEnergy(double specularWeight,double integratedSpecular) noexcept;
}
