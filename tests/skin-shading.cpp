#include "Renderer/Features/jSkinShadingMath.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

namespace
{
using namespace jSkinShading;
int Passed=0,Failed=0;
void Check(bool condition,const char* message)
{if(condition)++Passed;else{++Failed;std::cerr<<"FAIL: "<<message<<'\n';}}
bool Near(double a,double b,double tolerance=1.e-11)
{return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance*std::max({1.,std::abs(a),std::abs(b)});}

void SettingsCases()
{
    Settings requested,output;std::string error;
    Check(requested.ShadingModel==Model::StandardPBR&&ValidateSettings(requested,output,error)&&requested==output,
        "the default material keeps Standard PBR and validates without activating skin");
    requested.ShadingModel=Model::SkinKelemen;requested.SpecularWeight=8;
    Check(ValidateSettings(requested,output,error)&&output==requested,"the editor's maximum rho_s=8 is accepted unchanged");
    const auto original=output;
    requested.SpecularWeight=8.01f;
    Check(!ValidateSettings(requested,output,error)&&output==original&&!error.empty(),"over-budget specular weight preserves the last material settings");
    requested.SpecularWeight=std::numeric_limits<float>::quiet_NaN();
    Check(!ValidateSettings(requested,output,error)&&output==original,"nonfinite material weight is rejected transactionally");
    requested.SpecularWeight=-1;
    Check(!ValidateSettings(requested,output,error)&&output==original,"negative material weight is rejected transactionally");
    requested={};requested.ShadingModel=static_cast<Model>(2);
    Check(!ValidateSettings(requested,output,error)&&output==original,"an unknown model cannot activate a shader path");
}

void AnalyticCases()
{
    std::string error;double value=-1;
    Check(BeckmannPH(1.,.2,value,error)&&Near(value,25.),"Beckmann PH at normal incidence is 1/m^2 without an extra PI");
    Check(BeckmannPH(0.,.2,value,error)&&value==0.,"the tangent PH endpoint is finite zero instead of zero times infinity");
    const double angle=.4,m=.3;
    const double source=std::exp(-std::pow(std::tan(angle),2)/(m*m))/(m*m*std::pow(std::cos(angle),4));
    Check(BeckmannPH(std::cos(angle),m,value,error)&&Near(value,source),"PH agrees with the original acos/tan expression away from singularities");
    double positive=0,negative=0;
    Check(BeckmannPH(.7,.4,positive,error)&&BeckmannPH(-.7,.4,negative,error)&&Near(positive,negative),
        "PH retains the source's even cosine symmetry");
    const Vector3 n{0,0,1};
    Check(KelemenSpecular(n,n,n,.2,1.,value,error)&&Near(value,FresnelF0/(4.*.2*.2)),
        "normal-incidence Kelemen equals rho_s*F0/(4*m^2), including one cosine");
    double base=0,weighted=0;
    KelemenSpecular(n,n,n,.15,1.,base,error);
    Check(KelemenSpecular(n,n,n,.15,1.99,weighted,error)&&Near(weighted,base*1.99),
        "rho_s scales the specular lobe rather than replacing Fresnel F0");
    const Vector3 l{.3,.2,std::sqrt(.87)},v{-.2,.1,std::sqrt(.95)};
    double forward=0,reverse=0;
    Check(KelemenSpecular(n,l,v,.4,1.,forward,error)&&KelemenSpecular(n,v,l,.4,1.,reverse,error)&&
        Near(forward/l[2],reverse/v[2]),"the BRDF is reciprocal after removing each direction's single NoL factor");
    const Vector3 ls{l[0]*200,l[1]*200,l[2]*200},vs{v[0]*.01,v[1]*.01,v[2]*.01},ns{0,0,99};
    Check(KelemenSpecular(ns,ls,vs,.4,1.,value,error)&&Near(value,forward),"non-unit direction lengths do not change shading");
    Check(KelemenSpecular(n,{0,0,-1},n,.2,1.,value,error)&&value==0.,"back-facing illumination contributes no specular");
    Check(KelemenSpecular(n,n,{0,0,-1},.2,1.,value,error)&&value==0.,"an exactly opposite light/view pair has a safe zero half-vector");
    Check(KelemenSpecular({0,0,0},l,v,.2,1.,value,error)&&value==0.,"a finite zero normal safely returns zero shading");
    Check(KelemenSpecular(n,{0,0,0},v,.2,1.,value,error)&&value==0.,"a zero light direction cannot produce a NaN");
    Check(KelemenSpecular(n,l,v,.2,0.,value,error)&&value==0.,"zero specular weight is exactly diffuse-only");
    Check(KelemenSpecular({0,0,1.e300},{0,0,1.e-300},{0,0,1.e200},.2,1.,value,error)&&
        Near(value,FresnelF0/(4.*.2*.2)),"normalization avoids finite input overflow and underflow");
    bool finite=true;
    for(double cosine:{0.,1.e-8,.001,.1,.5,.99,1.})for(double roughness:{.02,.15,1.})
    {
        const Vector3 direction{std::sqrt(std::max(1.-cosine*cosine,0.)),0,cosine};
        finite&=KelemenSpecular(n,direction,direction,roughness,8.,value,error)&&std::isfinite(value)&&value>=0.;
    }
    Check(finite,"grazing and minimum-roughness cases remain finite and nonnegative");
}

void InvalidCases()
{
    std::string error;double value=42.;const Vector3 n{0,0,1};
    const double nan=std::numeric_limits<double>::quiet_NaN();
    Check(!BeckmannPH(1.01,.2,value,error)&&value==42.,"out-of-range cosine leaves the previous PH untouched");
    Check(!BeckmannPH(.5,.019,value,error)&&value==42.,"roughness below the shared bound is not silently changed by CPU validation");
    Check(!BeckmannPH(nan,.2,value,error)&&value==42.,"nonfinite PH input preserves the previous result");
    Check(!KelemenSpecular(n,n,n,1.01,1.,value,error)&&value==42.,"invalid Kelemen roughness preserves the previous result");
    Check(!KelemenSpecular({nan,0,1},n,n,.2,1.,value,error)&&value==42.,"nonfinite normal does not publish partial shading");
    Check(!KelemenSpecular(n,n,n,.2,8.01,value,error)&&value==42.,"weight above the editor contract is rejected");
    Check(!IntegrateSpecularEnergy(-.01,.2,value,error)&&value==42.&&!IntegrateSpecularEnergy(1.,nan,value,error)&&value==42.,
        "invalid energy coordinates are transactional before quadrature");
    Check(!EnergyTexel(64,0,value,error)&&value==42.&&!EnergyTexel(0,64,value,error)&&value==42.,
        "invalid texel indices cannot wrap around the fixed LUT");
}

void EnergyCases()
{
    std::string error;double value=0.;
    // Golden values use the original GLSL acos/tan formula and its exact
    // endpoint-inclusive 80x80 quadrature, evaluated separately in double.
    struct Golden {double Cosine,Roughness,Energy;};
    const Golden cases[]={{1.,.15,0.08417360309867887},{.5,.15,0.17730720450988666},
        {.1,.15,0.78836013304833774},{.5,.5,0.10803889093041734},{.5,1.,0.066853128460857378}};
    for(const auto& golden:cases)
        Check(IntegrateSpecularEnergy(golden.Cosine,golden.Roughness,value,error)&&Near(value,golden.Energy,1.e-10),
            "bounded quadrature agrees with the pinned source energy reference");
    bool bounded=true;
    for(std::uint32_t x:{0u,32u,63u})for(std::uint32_t y:{0u,9u,63u})
        bounded&=EnergyTexel(x,y,value,error)&&std::isfinite(value)&&value>=0.;
    Check(bounded,"LUT center coordinates, roughness floor and grazing corners stay finite");
    double mapped=0.,direct=0.;
    Check(EnergyTexel(32,9,mapped,error)&&IntegrateSpecularEnergy(32.5/64.,9.5/64.,direct,error)&&Near(mapped,direct),
        "CPU texel reference matches the actual GPU texel-center mapping");
    Check(Near(DiffuseEnergy(1.99,.2),.602),"source rho_s scales the integrated specular subtraction once");
    Check(DiffuseEnergy(8.,1.)==0.&&DiffuseEnergy(0.,1.)==1.&&DiffuseEnergy(8.,0.)==1.,
        "diffuse energy cannot become negative and zero specular preserves all diffuse");
    Check(DiffuseEnergy(-1.,.2)==1.&&DiffuseEnergy(1.,std::numeric_limits<double>::quiet_NaN())==1.,
        "invalid energy input fails open without darkening a material");
}
}

int RunSkinShadingTests()
{
    Passed=0;Failed=0;SettingsCases();AnalyticCases();InvalidCases();EnergyCases();
    std::cout<<"Skin shading: "<<Passed<<" passed, "<<Failed<<" failed\n";return Failed?1:0;
}
#ifndef JENGINE_SKIN_SHADING_TEST_NO_MAIN
int main(){return RunSkinShadingTests();}
#endif
