#include "jSceneFeatureCpuTests.h"
#include "Renderer/Features/jEnvironmentLighting.h"
#include "Renderer/Features/jWaterSimulation.h"
#include "Renderer/Features/jHorizonMapping.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
constexpr double Pi = 3.14159265358979323846;
struct Checks
{
    int Total = 0, Failed = 0;
    void Check(bool result, const char* label)
    {
        ++Total;
        if (!result) { ++Failed; std::cerr << "SceneFeatureCPU FAIL: " << label << '\n'; }
    }
};
bool Near(double a, double b, double epsilon = 1e-4) { return std::abs(a-b) <= epsilon; }
template <typename T> bool Near3(const T& a, const T& b, double epsilon = 1e-4)
{ return Near(a[0], b[0], epsilon) && Near(a[1], b[1], epsilon) && Near(a[2], b[2], epsilon); }
std::string Read(const std::filesystem::path& path)
{ std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
void Write(const std::filesystem::path& path, const std::string& text)
{ std::ofstream file(path, std::ios::binary); file << text; if (!file) throw std::runtime_error("Fixture write failed."); }
struct FixtureDirectory
{
    std::filesystem::path Path;
    FixtureDirectory()
    {
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 32; ++attempt)
        {
            Path = std::filesystem::temp_directory_path()/("jengine-scene-feature-"+std::to_string(token)+"-"+std::to_string(attempt));
            if (std::filesystem::create_directory(Path)) return;
        }
        throw std::runtime_error("Cannot create scene feature fixture.");
    }
    ~FixtureDirectory() { std::error_code ignored; std::filesystem::remove_all(Path, ignored); }
};

void TestEnvironment(Checks& check, const std::filesystem::path& root)
{
    namespace E = jEnvironmentLighting;
    std::string error;
    E::Coefficients constant{};
    const E::RGB hdr{2.f, 4.f, 8.f};
    check.Check(E::ProjectEquirectangular({1,1,&hdr,1}, constant, error), "SH projects one-texel full-sphere HDR");
    for (std::size_t c = 0; c < 3; ++c)
        check.Check(Near(constant[0][c], hdr[c]*std::sqrt(4.*Pi), 1e-5), "SH constant DC integral");
    bool noHigherBands = true;
    for (std::size_t i = 1; i < constant.size(); ++i)
        for (float c : constant[i]) noHigherBands = noHigherBands && std::abs(c) < 1e-5f;
    check.Check(noHigherBands, "SH constant has no directional leakage");
    for (const E::Direction direction : {E::Direction{1,0,0}, E::Direction{0,1,0}, E::Direction{0,0,-1}, E::Direction{1,2,3}})
    {
        check.Check(Near3(E::EvaluateRadiance(constant,direction),hdr,1e-5), "SH constant radiance independent of direction");
        E::RGB irradiance{float(2*Pi),float(4*Pi),float(8*Pi)};
        check.Check(Near3(E::EvaluateDiffuseIrradiance(constant,direction),irradiance,2e-5), "SH constant Lambert irradiance is pi*L");
    }
    constexpr std::uint32_t width=256, height=128;
    std::vector<E::RGB> pixels(width*height);
    for (std::uint32_t y=0;y<height;++y) for (std::uint32_t x=0;x<width;++x)
    {
        const double theta=Pi*(y+.5)/height, phi=2*Pi*(x+.5)/width;
        pixels[y*width+x]={float(2+.5*std::sin(theta)*std::cos(phi)),
            float(3+.25*std::cos(theta)),float(1+.4*std::sin(theta)*std::sin(phi))};
    }
    E::Coefficients analytic{};
    check.Check(E::ProjectEquirectangular({width,height,pixels.data(),pixels.size()},analytic,error), "SH analytic linear environment projects");
    check.Check(Near(analytic[3][0],.5*std::sqrt(4*Pi/3),2e-4)
        && Near(analytic[1][1],.25*std::sqrt(4*Pi/3),2e-4)
        && Near(analytic[2][2],.4*std::sqrt(4*Pi/3),2e-4), "SH xyz basis ordering and normalization");
    const E::Direction direction{1,2,3};
    const double length=std::sqrt(14.);
    const E::RGB expected{float(2+.5/length),float(3+.5/length),float(1+1.2/length)};
    const E::RGB expectedDiffuse{float(2*Pi+2*Pi/3*.5/length),float(3*Pi+2*Pi/3*.5/length),float(Pi+2*Pi/3*1.2/length)};
    check.Check(Near3(E::EvaluateRadiance(analytic,direction),expected,3e-4), "SH reconstructs directional analytic radiance");
    check.Check(Near3(E::EvaluateDiffuseIrradiance(analytic,direction),expectedDiffuse,5e-4), "SH cosine convolution applied exactly once");
    E::Coefficients retained=analytic;
    E::ProjectionOptions budget; budget.MaxTexels=1;
    check.Check(!E::ProjectEquirectangular({width,height,pixels.data(),pixels.size()},retained,error,budget)
        && retained==analytic, "SH texel budget failure preserves output");
    int cancellationChecks=0;
    E::ProjectionOptions cancel; cancel.Cancelled=[&] { return ++cancellationChecks>4; };
    check.Check(!E::ProjectEquirectangular({width,height,pixels.data(),pixels.size()},retained,error,cancel)
        && retained==analytic, "SH mid-projection cancellation preserves output");
    pixels[17][1]=std::numeric_limits<float>::quiet_NaN();
    check.Check(!E::ProjectEquirectangular({width,height,pixels.data(),pixels.size()},retained,error)
        && retained==analytic, "SH nonfinite HDR rejected transactionally");
    check.Check(!E::ProjectEquirectangular({width,height,pixels.data(),1},retained,error)
        && retained==analytic, "SH borrowed pixel count validated");
    check.Check(E::EvaluateRadiance(constant,{0,0,0})==E::RGB{}, "SH zero direction safely returns black");
    const auto asset=root/"studio.jsh";
    check.Check(E::SaveAsset(asset,constant,error) && E::LoadAsset(asset,retained,error)
        && retained==constant, "SH versioned asset exact round trip");
    check.Check(E::SaveAsset(asset,analytic,error) && E::LoadAsset(asset,retained,error)
        && retained==analytic, "SH atomic asset replacement");
    const auto original=Read(asset);
    auto invalid=analytic; invalid[0][0]=std::numeric_limits<float>::infinity();
    check.Check(!E::SaveAsset(asset,invalid,error) && Read(asset)==original, "SH invalid save preserves previous asset");
    const auto nulPath=root/(std::string("studio.jsh")+std::string(1,'\0')+"suffix");
    check.Check(!E::LoadAsset(nulPath,retained,error) && retained==analytic,"SH asset path NUL cannot alias an existing asset");
    check.Check(!E::SaveAsset(nulPath,constant,error) && Read(asset)==original,"SH invalid asset path preserves existing file");
    const auto malformed=root/"invalid.jsh";
    for (const auto& replacement : {std::pair<std::string,std::string>{"\"version\": 1","\"version\": 2"},
        {"world-y-up","camera-y-up"},{"linear-radiance","display-srgb"},{"real-sh-l2-sloan-xyz","other-basis"}})
    {
        auto text=original; const auto position=text.find(replacement.first);
        if (position==std::string::npos) throw std::runtime_error("Cannot locate asset fixture field.");
        text.replace(position,replacement.first.size(),replacement.second); Write(malformed,text);
        retained=analytic;
        check.Check(!E::LoadAsset(malformed,retained,error) && retained==analytic, "SH incompatible version/space/units/basis rejected");
    }
    auto duplicate=original; duplicate.insert(1,"\"version\":1,"); Write(malformed,duplicate);
    check.Check(!E::LoadAsset(malformed,retained,error) && retained==analytic, "SH duplicate asset field rejected");
    Write(malformed,std::string(E::MaximumAssetBytes+1,' '));
    check.Check(!E::LoadAsset(malformed,retained,error) && retained==analytic, "SH oversized asset rejected");
    Write(malformed,std::string(128,'[')+std::string(128,']'));
    check.Check(!E::LoadAsset(malformed,retained,error) && retained==analytic, "SH nesting limit rejects malformed payload");
    bool noStaging=true;
    for (const auto& entry:std::filesystem::directory_iterator(root))
        noStaging=noStaging && entry.path().filename().string().find(".tmp-")==std::string::npos;
    check.Check(noStaging,"SH atomic save leaves no staging files");
}

void TestWater(Checks& check)
{
    namespace W=jWaterSimulation;
    std::string error;
    W::Settings flat; flat.BaseHeight=3.f; flat.Waves.push_back({});
    W::SurfaceSample sample;
    check.Check(W::Evaluate(flat,{2,-4},17.5,sample,error) && sample.Position==W::Vec3{2,3,-4}
        && sample.Normal==W::Vec3{0,1,0} && sample.Velocity==W::Vec3{}, "Water zero amplitude is stationary flat plane");
    W::Settings waves; waves.BaseHeight=2.f;
    waves.Waves={{{4,0},.6f,8.f,2.f,.4f,.2f},{{.6f,.8f},.3f,5.f,-.5f,.2f,-.1f}};
    check.Check(W::Evaluate(waves,{2.3f,-1.2f},1.7,sample,error), "Water mixed Gerstner waves evaluate");
    W::SurfaceSample paused;
    check.Check(W::Evaluate(waves,{2.3f,-1.2f},1.7,paused,error) && paused.Position==sample.Position
        && paused.Normal==sample.Normal && paused.Velocity==sample.Velocity, "Water pause reuses absolute time with no drift");
    constexpr float epsilon=.001f;
    W::SurfaceSample xp,xm,zp,zm,tp,tm;
    W::Evaluate(waves,{2.3f+epsilon,-1.2f},1.7,xp,error); W::Evaluate(waves,{2.3f-epsilon,-1.2f},1.7,xm,error);
    W::Evaluate(waves,{2.3f,-1.2f+epsilon},1.7,zp,error); W::Evaluate(waves,{2.3f,-1.2f-epsilon},1.7,zm,error);
    W::Evaluate(waves,{2.3f,-1.2f},1.7+epsilon,tp,error); W::Evaluate(waves,{2.3f,-1.2f},1.7-epsilon,tm,error);
    W::Vec3 dx{},dz{},velocity{};
    for (std::size_t c=0;c<3;++c)
    { dx[c]=(xp.Position[c]-xm.Position[c])/(2*epsilon); dz[c]=(zp.Position[c]-zm.Position[c])/(2*epsilon);
      velocity[c]=(tp.Position[c]-tm.Position[c])/(2*epsilon); }
    check.Check(Near3(sample.TangentX,dx,.0005) && Near3(sample.TangentZ,dz,.0005), "Water analytic tangents match finite differences");
    W::Vec3 normal{dz[1]*dx[2]-dz[2]*dx[1],dz[2]*dx[0]-dz[0]*dx[2],dz[0]*dx[1]-dz[1]*dx[0]};
    const float length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
    for(float& c:normal)c/=length;
    check.Check(Near3(sample.Normal,normal,.0005) && sample.Normal[1]>0, "Water analytic normal matches geometry and faces up");
    check.Check(Near3(sample.Velocity,velocity,.0005), "Water analytic velocity matches time derivative");
    auto normalized=waves; normalized.Waves[0].Direction={1,0};
    W::SurfaceSample normalizedSample;
    check.Check(W::Evaluate(normalized,{2.3f,-1.2f},1.7,normalizedSample,error)
        && normalizedSample.Position==sample.Position, "Water direction magnitude does not alter phase or displacement");
    W::Bounds bounds;
    check.Check(W::ConservativeBounds(waves,{-4,-3},{5,6},bounds,error), "Water conservative world bounds build");
    bool contained=true;
    for(int t=0;t<9;++t)for(int y=0;y<5;++y)for(int x=0;x<5;++x)
    {
        W::SurfaceSample p;
        contained=contained && W::Evaluate(waves,{-4.f+x*2.25f,-3.f+y*2.25f},t*.73,p,error);
        for(std::size_t c=0;c<3;++c)contained=contained && p.Position[c]>=bounds.Min[c] && p.Position[c]<=bounds.Max[c];
    }
    check.Check(contained,"Water time-varying displaced samples stay inside conservative bounds");
    for(int kind=0;kind<5;++kind)
    {
        auto bad=waves;
        if(kind==0)bad.Waves[0].Wavelength=0;
        if(kind==1)bad.Waves[0].Amplitude=-1;
        if(kind==2)bad.Waves[0].Direction={0,0};
        if(kind==3)bad.Waves.resize(W::MaximumWaves+1);
        if(kind==4){bad.Waves[0].Wavelength=1;bad.Waves[0].Amplitude=2;bad.Waves[0].Steepness=1;}
        paused=sample;
        check.Check(!W::Evaluate(bad,{0,0},0,paused,error) && paused.Position==sample.Position
            && paused.Normal==sample.Normal, "Water invalid/folding profile preserves previous surface result");
    }
    check.Check(!W::Evaluate(waves,{0,0},std::numeric_limits<double>::quiet_NaN(),paused,error)
        && paused.Position==sample.Position,"Water nonfinite absolute time rejected");
    const auto retainedBounds=bounds;
    check.Check(!W::ConservativeBounds(waves,{5,0},{-4,1},bounds,error)
        && bounds.Min==retainedBounds.Min && bounds.Max==retainedBounds.Max,"Water inverted bounds rejected transactionally");
}

void TestHorizon(Checks& check)
{
    namespace H=jHorizonMapping;
    std::string error;
    std::vector<float> heights(15,.25f);
    H::HeightFieldView field{5,3,heights.data(),heights.size(),2,1,1};
    H::BakeSettings settings; settings.RadiusTexels=4;
    H::HorizonMap flat;
    for(const auto edge:{H::Boundary::Clamp,H::Boundary::Wrap})
    {
        settings.Edges=edge;
        check.Check(H::Bake(field,settings,flat,error),"Horizon non-power-of-two flat heightfield bakes");
        bool clear=true;
        for(std::size_t i=0;i<flat.Angles.size();++i)
        {for(float angle:flat.Angles[i])clear=clear && angle==0;clear=clear && flat.AmbientVisibility[i]==1;}
        check.Check(clear,"Horizon flat field has zero horizons and full sky visibility");
    }
    H::EncodedLayers encoded;
    check.Check(H::EncodeLayers(flat,encoded,error) && encoded.Layer0.size()==15 && encoded.Layer1.size()==15
        && encoded.Layer0[0]==std::array<float,4>{0,0,0,0} && encoded.AmbientVisibility[0]==1,
        "Horizon float RGBA layer encoding preserves flat field");
    std::fill(heights.begin(),heights.end(),0.f);
    for(std::uint32_t y=0;y<3;++y)heights[y*5+3]=1;
    settings.Edges=H::Boundary::Clamp;
    H::HorizonMap ridge;
    check.Check(H::Bake(field,settings,ridge,error)
        && Near(ridge.Angles[7][0],std::atan2(2.,1.),1e-6)
        && ridge.Angles[7][4]==0 && ridge.AmbientVisibility[7]<1,
        "Horizon ridge has analytic east blocker angle and reduces ambient visibility");
    check.Check(H::EncodeLayers(ridge,encoded,error)
        && Near(encoded.Layer0[7][0],std::atan2(2.,1.)/(Pi*.5),1e-6),"Horizon layers encode radians divided by pi/2");
    settings.RadiusTexels=2; settings.Edges=H::Boundary::Wrap;
    H::HorizonMap wrapped;
    check.Check(H::Bake(field,settings,wrapped,error) && Near(wrapped.Angles[5][4],std::atan2(2.,2.),1e-6),
        "Horizon arbitrary-width wrap reaches correct negative-coordinate texel");
    settings.Edges=H::Boundary::Clamp;
    H::HorizonMap clamped;
    check.Check(H::Bake(field,settings,clamped,error) && clamped.Angles[5][4]==0,
        "Horizon clamp does not wrap across image edge");
    const auto saved=wrapped;
    const auto same=[&] {return wrapped.Width==saved.Width && wrapped.Height==saved.Height
        && wrapped.Angles==saved.Angles && wrapped.AmbientVisibility==saved.AmbientVisibility;};
    for(int kind=0;kind<7;++kind)
    {
        auto badField=field; auto badSettings=settings;
        if(kind==0)badSettings.MaxSampleSteps=1;
        if(kind==1)badField.SampleCount=1;
        if(kind==2)badField.TexelSizeU=0;
        if(kind==3)badSettings.RadiusTexels=0;
        if(kind==4)badSettings.Edges=static_cast<H::Boundary>(5);
        if(kind==5)badField.HeightScale=std::numeric_limits<float>::infinity();
        if(kind==6)badSettings.MaxTexels=1;
        check.Check(!H::Bake(badField,badSettings,wrapped,error) && same(),"Horizon invalid input/budgets preserve previous bake");
    }
    heights[3]=std::numeric_limits<float>::quiet_NaN();
    check.Check(!H::Bake(field,settings,wrapped,error) && same(),"Horizon nonfinite height rejected transactionally"); heights[3]=1;
    int polls=0; settings.Cancelled=[&] {return ++polls>8;};
    check.Check(!H::Bake(field,settings,wrapped,error) && same(),"Horizon mid-bake cancellation preserves previous map");
    settings.Cancelled=[]()->bool{throw std::runtime_error("cancel provider failed");};
    check.Check(!H::Bake(field,settings,wrapped,error) && same(),"Horizon cancellation callback failure preserves previous map");
    auto invalidMap=ridge; invalidMap.Angles[0][0]=4;
    const auto savedLayer=encoded.Layer0;
    check.Check(!H::EncodeLayers(invalidMap,encoded,error) && encoded.Layer0==savedLayer,
        "Horizon invalid encoding input preserves previous layers");
}
}

int RunSceneFeatureCpuTests()
{
    Checks check;
    try
    {
        FixtureDirectory fixture;
        TestEnvironment(check,fixture.Path);
        TestWater(check);
        TestHorizon(check);
    }
    catch(const std::exception& exception)
    {check.Check(false,exception.what());}
    std::cout << "Scene feature CPU tests: " << check.Total-check.Failed << "/" << check.Total << " passed\n";
    return check.Failed ? 1 : 0;
}
