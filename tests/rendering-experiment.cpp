#include "Renderer/Experiments/jRenderingExperiment.h"
#include "Renderer/Experiments/jExperimentImage.h"
#include <iostream>
#include <limits>
#include <cmath>
#include <chrono>

int main()
{
    namespace ex=jRenderingExperiment;int failures=0;
    const auto check=[&](bool ok,const char* name){if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
    std::string error;auto definition=ex::ShadowDefaults();
    check(ex::Validate(definition,error),"default experiment");
    ex::Definition parsed;check(ex::Parse(ex::Serialize(definition),parsed,error),"JSON round trip");
    check(ex::Serialize(definition)==ex::Serialize(parsed),"round-trip semantics");
    auto cameraDefinition=definition;cameraDefinition.Camera=ex::CameraSettings{};
    check(ex::Parse(ex::Serialize(cameraDefinition),parsed,error)&&parsed.Camera&&parsed.Camera->Position==cameraDefinition.Camera->Position,"saved camera round trip");
    cameraDefinition.Camera->Target=cameraDefinition.Camera->Position;
    check(!ex::Validate(cameraDefinition,error),"reject degenerate saved camera");
    auto bad=definition;bad.Variants[1].Id="../unsafe";check(!ex::Validate(bad,error),"reject unsafe artifact IDs");
    bad=definition;bad.Variants[1].Id=bad.Variants[0].Id;check(!ex::Validate(bad,error),"reject duplicate IDs");
    bad=definition;bad.Common["resolution"]=2048;check(!ex::Validate(bad,error),"respect renderer resolution limit");
    bad=definition;bad.Common["unknown"]=1;check(!ex::Validate(bad,error),"reject unknown parameters");
    bad=definition;bad.Region={.9,0,.2,1};check(!ex::Validate(bad,error),"reject overflowing ROI");
    bad=definition;bad.Variants[0].Enabled=false;check(!ex::Validate(bad,error),"baseline must be enabled");
    bad=definition;bad.Variants[0].Overrides["resolution"]=128;check(ex::Validate(bad,error),"bounded per-variant override");
    check(ex::ResolvedParameters(bad,bad.Variants[0]).at("resolution")==128,"resolved override");
    const auto previous=ex::Serialize(parsed);check(!ex::Parse("{}",parsed,error)&&ex::Serialize(parsed)==previous,"failed parse is transactional");
    auto json=ex::Serialize(definition);const auto position=json.find("\"measuredFrames\": 60");
    json.replace(position,std::string("\"measuredFrames\": 60").size(),"\"measuredFrames\": -1");
    check(!ex::Parse(json,parsed,error),"negative frame count does not wrap");
    definition.Repetitions=2;const auto plan=ex::MakePlan(definition);
    check(plan.size()==8&&plan[0].VariantIndex==0&&plan[4].VariantIndex==3&&plan.back().VariantIndex==0,"balanced repeat order");
    const double values[]={1,2,3,4,5};const auto summary=ex::Summarize(values);
    check(summary&&summary->Median==3&&std::abs(summary->P95-4.8)<1e-9,"percentile interpolation");
    const double invalid[]={1,std::numeric_limits<double>::quiet_NaN()};
    check(!ex::Summarize(invalid)&&!ex::Summarize({}),"invalid/empty timing is not zero");
    const float a[]={1,2,3,1,4,5,6,1},b[]={2,3,4,0,4,5,6,0};
    auto diff=ex::Compare(a,b,2,1,{0,0,1,1});
    check(diff.Valid&&diff.Pixels==2&&diff.Mae==.5&&std::abs(diff.Rmse-std::sqrt(.5))<1e-9,"linear RGB difference ignores alpha");
    diff=ex::Compare(a,b,2,1,{.5,0,.5,1});check(diff.Valid&&diff.Mae==0,"ROI excludes other pixels");
    float nan[]={1,2,std::numeric_limits<float>::infinity(),1,4,5,6,1};
    diff=ex::Compare(a,nan,2,1,{0,0,1,1});check(!diff.Valid&&diff.NonFinitePixels==1,"nonfinite image is invalid");
    check(ex::EscapeHtml("<&\"'>")=="&lt;&amp;&quot;&#39;&gt;","HTML escaping");
    const auto directory=std::filesystem::temp_directory_path()/("jengine-lab-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    try
    {
        check(ex::Save(directory/"experiment.jlab",definition,error),"save experiment");
        definition.Name="Updated";check(ex::Save(directory/"experiment.jlab",definition,error),"atomic replacement");
        check(ex::Load(directory/"experiment.jlab",parsed,error)&&parsed.Name=="Updated","load updated experiment");
        auto invalidSave=definition;invalidSave.Variants[0].Id="../escape";
        check(!ex::Save(directory/"experiment.jlab",invalidSave,error)&&ex::Load(directory/"experiment.jlab",parsed,error)&&parsed.Name=="Updated","failed save preserves previous experiment");
        ex::FloatImage image{2,1,std::vector<float>(std::begin(a),std::end(a))};
        ex::SavePfm(directory/"linear.pfm",image);const auto loaded=ex::LoadPfm(directory/"linear.pfm");
        check(loaded.Width==2&&loaded.Height==1&&loaded.Rgba==image.Rgba,"PFM float round trip");
        const auto display=ex::Display(image,0);check(display.Rgba.size()==8&&display.Rgba[3]==255,"bounded display conversion");
    }
    catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::filesystem::remove_all(directory);
    std::cout<<"Rendering experiment checks: "<<failures<<" failures\n";return failures?1:0;
}
