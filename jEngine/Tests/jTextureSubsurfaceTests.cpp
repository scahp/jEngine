#include "jTextureSubsurfaceTests.h"
#include "Renderer/Features/jTextureSubsurfaceMath.h"
#include <algorithm>
#include <iostream>
#include <limits>

int RunTextureSubsurfaceTests()
{
    using namespace jTextureSubsurface;
    int checks=0,failures=0;
    const auto require=[&](bool condition,const char* message)
    {++checks;if(!condition){++failures;std::cerr<<"FAIL texture SSS: "<<message<<'\n';}};
    const auto nearlyEqual=[](float a,float b){return std::abs(a-b)<2.e-6f;};
    std::string error;Settings settings,validated;
    const float receiverLength=std::sqrt(10100.f);
    const RGB receiverRay{10.f/receiverLength,0.f,100.f/receiverLength};
    float plane=17.f;
    require(receiverLength>100.f+.05f,"negative control: raw neighboring radial depth self-shadows a flat plane");
    require(ReceiverPlaneDistance({0,0,0},receiverRay,{0,0,100},{0,0,-1},200,plane)&&
        std::abs(plane-receiverLength)<.00005f,"receiver-plane correction restores actual depth on a different finite-light ray");
    require(ReceiverPlaneDistance({10,0,0},{0,0,1},{0,0,100},{0,0,-1},200,plane)&&plane==100.f,
        "directional parallel rays intersect the captured world plane");
    require(ReceiverPlaneDistance({0,0,0},{0,0,1},{0,0,90},{0,0,-1},200,plane)&&105.f>plane+.05f,
        "a separate real occluder remains shadowing after plane correction");
    plane=17.f;
    require(!ReceiverPlaneDistance({0,0,0},{1,0,0},{0,0,100},{0,0,-1},200,plane)&&plane==17.f,
        "grazing capture plane rejects transactionally for local fallback");
    require(!ReceiverPlaneDistance({0,0,0},{0,0,1},{0,0,100},{0,0,-1},50,plane)&&plane==17.f,
        "out-of-capture intersection cannot invent an entry");
    require(!ReceiverPlaneDistance({0,0,0},{0,0,1},{0,0,-1},{0,0,-1},200,plane)&&plane==17.f,
        "negative intersection cannot donate transmission");
    require(ValidateSettings(settings,validated,error),"bounded defaults validate");
    require(settings.WorldUnitsPerMillimeter==1.f,"generic units have no hidden source-model scale");
    for(unsigned invalid=0;invalid<8;++invalid)
    {
        auto bad=settings;
        if(invalid==0) bad.TileResolution=2048;
        if(invalid==1) bad.CaptureResolution=127;
        if(invalid==2) bad.GaussianStepScale=std::numeric_limits<float>::quiet_NaN();
        if(invalid==3) bad.WorldUnitsPerMillimeter=0;
        if(invalid==4) bad.TransmissionExtinctionPerMillimeter=0;
        if(invalid==5) bad.TransmissionThicknessScale=-1;
        if(invalid==6) bad.CaptureBiasWorld=std::numeric_limits<float>::infinity();
        if(invalid==7) bad.TileResolution=1000;
        validated.WorldUnitsPerMillimeter=2.f;
        require(!ValidateSettings(bad,validated,error)&&validated.WorldUnitsPerMillimeter==2.f,
            "invalid physical/resolution input rejects transactionally");
    }
    const auto weights=NormalizedLobeWeights();RGB total{};
    float accumulatedVariance=0.f;
    for(unsigned level=0;level<6;++level)
    {
        const auto sigma=IncrementalSigmaMm(level);accumulatedVariance+=sigma*sigma;
        require(nearlyEqual(accumulatedVariance,VarianceMm2[level]),"Gaussian convolution adds variance in mm squared");
        for(unsigned c=0;c<3;++c)
        {require(std::isfinite(weights[level][c])&&weights[level][c]>=0.f,"lobes have finite positive channel energy");total[c]+=weights[level][c];}
    }
    for(unsigned c=0;c<3;++c) require(nearlyEqual(total[c],1.f),"constant irradiance is preserved per channel");
    require(IncrementalSigmaMm(6)==0.f&&IncrementalSigmaMm(999)==0.f,"invalid Gaussian level is bounded");
    const auto impulseVariance=[&](unsigned c){float x=0.f;for(unsigned l=0;l<6;++l)x+=weights[l][c]*VarianceMm2[l];return x;};
    require(impulseVariance(0)>impulseVariance(1)&&impulseVariance(1)>impulseVariance(2),"skin transports red farther than green/blue");
    for(float mm:{.01f,.5f,1.f,5.f,20.f})
        require(std::abs(DecodeTransmission(EncodeTransmission(mm,.2f,1.f),.2f)-mm)<.00001f,
            "transmission encoding and decoding use the same explicit units/extinction");
    require(EncodeTransmission(0,.2f,1.f)==1.f&&EncodeTransmission(1,.2f,0.f)==1.f,"zero optical thickness is exact unit transmission");
    require(EncodeTransmission(10000,.2f,1.f)>0.f&&std::isfinite(DecodeTransmission(0,.2f)),"thick/missing transmission remains finite");
    require(nearlyEqual(CorrectedThicknessMm(3,.3f,-.5f,1.f),10.f),"aligned entry normal does not invent a correction");
    require(nearlyEqual(CorrectedThicknessMm(3,.3f,-.5f,-1.f),5.f),"sampled opposing entry normal changes the backface correction");
    require(nearlyEqual(CorrectedThicknessMm(6,.6f,-.5f,-1.f),5.f),"world-scale/mm conversion leaves physical transport invariant");
    require(CorrectedThicknessMm(1,0,-1,-1)==0.f,"invalid unit conversion is finite and inactive");
    require(RangeAttenuation(1,0)==1.f&&RangeAttenuation(0,10)==1.f&&RangeAttenuation(100,10)==0.f,
        "point-light window exactly matches unbounded/center/range limits");
    require(nearlyEqual(RangeAttenuation(25,10),.87890625f),"point-light smooth range matches existing d squared convention");
    std::size_t bytes=123;
    require(ValidateWorkExtent(1,1024,1024,1280,720,bytes,error)&&bytes==std::size_t(1024)*1024*116u+1280u*720u*16u+16u,
        "one-surface work accounting includes all atlas/capture/output targets");
    require(ValidateWorkExtent(4,1024,1024,3840,2160,bytes,error),"four-surface 4K output fits the explicit budget");
    bytes=123;
    require(!ValidateWorkExtent(4,1024,2048,3840,2160,bytes,error)&&bytes==123,"large capture/output rejects before allocation transactionally");
    require(!ValidateWorkExtent(5,1024,1024,1,1,bytes,error)&&!ValidateWorkExtent(0,1024,1024,1,1,bytes,error),"surface count is bounded");
    require(!ValidateWorkExtent(1,1024,1024,std::numeric_limits<unsigned>::max(),1,bytes,error),"overflow-sized output rejects before multiplication");
    std::cout<<"Texture subsurface CPU: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
#ifdef JENGINE_TEXTURE_SUBSURFACE_TEST_MAIN
int main(){return RunTextureSubsurfaceTests();}
#endif
