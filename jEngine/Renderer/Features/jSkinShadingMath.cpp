#include "jSkinShadingMath.h"
#include <algorithm>
#include <limits>
#include <numbers>

namespace jSkinShading
{
namespace
{
constexpr double Pi=std::numbers::pi_v<double>;
bool RoughnessValid(double value)
{return std::isfinite(value)&&value>=MinimumRoughness&&value<=MaximumRoughness;}
bool Finite(const Vector3& value)
{return std::all_of(value.begin(),value.end(),[](double x){return std::isfinite(x);});}
double Dot(const Vector3& a,const Vector3& b)
{return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool Normalize(const Vector3& input,Vector3& output)
{
    const double scale=std::max({std::abs(input[0]),std::abs(input[1]),std::abs(input[2])});
    if(scale==0.)return false;
    Vector3 value{input[0]/scale,input[1]/scale,input[2]/scale};
    const double length=std::sqrt(Dot(value,value));
    for(std::size_t i=0;i<3;++i)output[i]=value[i]/length;
    return true;
}
double PH(double normalDotHalf,double roughness)
{
    // This is Shadows' PH, which intentionally does not contain 1/PI.
    // tan(acos(x))^2=(1-x*x)/(x*x) avoids the singular acos/tan endpoint.
    const double cosine=std::clamp(std::abs(normalDotHalf),0.,1.);
    if(cosine<=1.e-6)return 0.;
    const double cosine2=cosine*cosine,m2=roughness*roughness;
    return std::exp(-(1.-cosine2)/(cosine2*m2))/(m2*cosine2*cosine2);
}
double Specular(const Vector3& normal,const Vector3& light,const Vector3& view,double roughness,double weight)
{
    const double normalDotLight=Dot(normal,light);
    if(normalDotLight<=0.||weight==0.)return 0.;
    const Vector3 half{light[0]+view[0],light[1]+view[1],light[2]+view[2]};
    const double halfLength2=Dot(half,half);
    if(halfLength2<=1.e-12)return 0.;
    const double inverseLength=1./std::sqrt(halfLength2);
    const Vector3 halfUnit{half[0]*inverseLength,half[1]*inverseLength,half[2]*inverseLength};
    const double base=1.-std::clamp(Dot(view,halfUnit),0.,1.);
    const double fresnel=FresnelF0+(1.-FresnelF0)*base*base*base*base*base;
    return normalDotLight*weight*PH(Dot(normal,halfUnit),roughness)*fresnel/halfLength2;
}
}

bool BeckmannPH(double normalDotHalf,double roughness,double& output,std::string& error)
{
    error.clear();
    if(!std::isfinite(normalDotHalf)||normalDotHalf< -1.||normalDotHalf>1.||!RoughnessValid(roughness))
    {error="Beckmann PH requires finite cosine [-1,1] and roughness m [0.02,1].";return false;}
    output=PH(normalDotHalf,roughness);return true;
}

bool KelemenSpecular(const Vector3& normal,const Vector3& toLight,const Vector3& toEye,
    double roughness,double specularWeight,double& output,std::string& error)
{
    error.clear();
    if(!Finite(normal)||!Finite(toLight)||!Finite(toEye)||!RoughnessValid(roughness)||
        !std::isfinite(specularWeight)||specularWeight<0.||specularWeight>MaximumSpecularWeight)
    {error="Kelemen specular requires finite directions, m [0.02,1] and rho_s [0,8].";return false;}
    Vector3 n,l,v;
    if(!Normalize(normal,n)||!Normalize(toLight,l)||!Normalize(toEye,v))
    {output=0.;return true;}
    output=Specular(n,l,v,roughness,specularWeight);return true;
}

bool IntegrateSpecularEnergy(double normalDotView,double roughness,double& output,std::string& error)
{
    error.clear();
    if(!std::isfinite(normalDotView)||normalDotView<0.||normalDotView>1.||!RoughnessValid(roughness))
    {error="Skin energy requires finite cos(view) [0,1] and m [0.02,1].";return false;}
    const Vector3 normal{0,0,1},view{0,std::sqrt(std::max(1.-normalDotView*normalDotView,0.)),normalDotView};
    double sum=0.;
    for(std::uint32_t i=0;i<IntegrationSteps;++i)
    {
        const double phi=double(i)/double(IntegrationSteps-1)*2.*Pi;
        double local=0.;
        for(std::uint32_t j=0;j<IntegrationSteps;++j)
        {
            const double theta=double(j)/double(IntegrationSteps-1)*Pi*.5;
            const double sine=std::sin(theta);
            const Vector3 light{std::sin(phi)*sine,std::cos(phi)*sine,std::cos(theta)};
            local+=Specular(normal,light,view,roughness,1.)*sine;
        }
        sum+=local*(Pi*.5)/double(IntegrationSteps);
    }
    output=sum*(2.*Pi)/double(IntegrationSteps);return true;
}

bool EnergyTexel(std::uint32_t x,std::uint32_t y,double& output,std::string& error)
{
    error.clear();
    if(x>=EnergyLUTResolution||y>=EnergyLUTResolution)
    {error="Skin energy texel lies outside the bounded 64 x 64 LUT.";return false;}
    const double cosine=(double(x)+.5)/double(EnergyLUTResolution);
    const double roughness=std::clamp((double(y)+.5)/double(EnergyLUTResolution),MinimumRoughness,MaximumRoughness);
    return IntegrateSpecularEnergy(cosine,roughness,output,error);
}

double DiffuseEnergy(double specularWeight,double integratedSpecular) noexcept
{
    if(!std::isfinite(specularWeight)||!std::isfinite(integratedSpecular)||specularWeight<0.||
        specularWeight>MaximumSpecularWeight||integratedSpecular<0.)return 1.;
    return std::max(1.-specularWeight*integratedSpecular,0.);
}
}
