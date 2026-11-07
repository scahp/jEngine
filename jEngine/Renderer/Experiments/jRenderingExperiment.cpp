#include "jRenderingExperiment.h"
#include "External/PathTracingDataLoader/json.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <fstream>
#include <chrono>

namespace jRenderingExperiment
{
namespace
{
constexpr Algorithm Catalog[] = {{"shadow.pcf", "Poisson PCF", "shadows"},
    {"shadow.vsm", "VSM", "shadows"}, {"shadow.esm", "ESM", "shadows"}, {"shadow.evsm", "EVSM", "shadows"}};
bool Id(const std::string& text)
{
    return !text.empty() && text.size() <= 64 && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
bool RegionValid(const std::array<double, 4>& region)
{
    for (double value : region) if (!std::isfinite(value) || value < 0 || value > 1) return false;
    return region[2] > 0 && region[3] > 0 && region[0] + region[2] <= 1 && region[1] + region[3] <= 1;
}
}
std::span<const Algorithm> Algorithms() { return Catalog; }
const Algorithm* FindAlgorithm(const std::string& id)
{
    for (const auto& algorithm : Catalog) if (id == algorithm.Id) return &algorithm;
    return nullptr;
}
Definition ShadowDefaults()
{
    Definition d;
    d.Common = {{"resolution",512},{"maxDistance",1200},{"casterPadding",100},
        {"pcfRadius",1.5},{"blurRadius",2},{"positiveExponent",32},{"negativeExponent",8},
        {"minimumVariance",1e-5},{"bleedReduction",.2}};
    for (const auto& algorithm : Catalog)
        d.Variants.push_back({std::string(algorithm.Id).substr(7),algorithm.Name,algorithm.Id,{},true});
    return d;
}
Parameters ResolvedParameters(const Definition& definition, const Variant& variant)
{
    auto values = definition.Common;
    for (const auto& [key, value] : variant.Overrides) values[key] = value;
    return values;
}
bool Validate(const Definition& d, std::string& error)
{
    const auto fail = [&](const char* message) { error = message; return false; };
    if (d.Schema != 1 || d.Family != "shadows") return fail("Unsupported experiment schema or family.");
    if (d.Name.empty() || d.Name.size() > 128 || d.Variants.empty() || d.Variants.size() > 8)
        return fail("Use a name of 1..128 bytes and 1..8 variants.");
    if (d.WarmupFrames < 1 || d.WarmupFrames > 600 || d.MeasuredFrames < 4 || d.MeasuredFrames > 3600 ||
        d.Repetitions < 1 || d.Repetitions > 5) return fail("Frame/repetition budget is outside the supported limits.");
    if (!std::isfinite(d.FixedStep) || d.FixedStep < .001 || d.FixedStep > .1 ||
        !std::isfinite(d.MotionDegrees) || d.MotionDegrees < 0 || d.MotionDegrees > 60 ||
        !std::isfinite(d.DisplayExposure) || d.DisplayExposure < -16 || d.DisplayExposure > 16 ||
        uint32_t(d.Motion) > 2 || !RegionValid(d.Region)) return fail("Invalid time step, scenario, exposure or comparison region.");
    if (d.Camera)
    {
        const auto& c=*d.Camera;double distance=0;
        for(size_t i=0;i<3;++i)
        {
            if(!std::isfinite(c.Position[i])||!std::isfinite(c.Target[i])||std::abs(c.Position[i])>1e7||std::abs(c.Target[i])>1e7)
                return fail("Camera coordinates must be finite and bounded");
            distance+=(c.Position[i]-c.Target[i])*(c.Position[i]-c.Target[i]);
        }
        if(distance<1e-8||!std::isfinite(c.FovDegrees)||c.FovDegrees<5||c.FovDegrees>150||
            !std::isfinite(c.NearClip)||c.NearClip<=0||!std::isfinite(c.FarClip)||c.FarClip<=c.NearClip||c.FarClip>1e7||
            !std::isfinite(c.Aspect)||c.Aspect<0||c.Aspect>10)
            return fail("Invalid saved camera");
    }
    const auto defaults = ShadowDefaults().Common;
    const auto validateParameters = [&](const Parameters& params) {
        if (params.size() > defaults.size()) return false;
        for (const auto& [key,value] : params)
            if (!defaults.contains(key) || !std::isfinite(value)) return false;
        return true;
    };
    if (!validateParameters(d.Common) || d.Common.size() != defaults.size()) return fail("Missing, unknown or nonfinite common parameters.");
    std::set<std::string> ids; size_t enabled = 0; bool baseline = false;
    for (const auto& variant : d.Variants)
    {
        const auto* algorithm = FindAlgorithm(variant.AlgorithmId);
        if (!Id(variant.Id) || !ids.insert(variant.Id).second || variant.Label.empty() || variant.Label.size() > 128 ||
            !algorithm || d.Family != algorithm->Family || !validateParameters(variant.Overrides))
            return fail("Invalid variant ID, label, algorithm or parameters.");
        const auto p = ResolvedParameters(d,variant);
        const auto in = [&](const char* key,double lo,double hi) { return p.at(key) >= lo && p.at(key) <= hi; };
        const auto resolution = p.at("resolution"); const auto integerResolution = uint32_t(std::clamp(resolution,0.,1024.));
        if (!in("resolution",64,1024) || resolution != integerResolution || (integerResolution & (integerResolution - 1)) ||
            !in("maxDistance",1,1e6) || !in("casterPadding",0,1e5) || !in("pcfRadius",0,4) ||
            !in("blurRadius",0,8) || p.at("blurRadius") != std::floor(p.at("blurRadius")) ||
            !in("positiveExponent",.1,40) || !in("negativeExponent",.1,40) ||
            !in("minimumVariance",1e-8,1) || !in("bleedReduction",0,.999))
            return fail("Shadow parameters are outside the production adapter's limits.");
        if (variant.Enabled) { ++enabled; baseline |= variant.Id == d.Baseline; }
    }
    if (!enabled || !baseline) return fail("Enable at least one variant and its chosen baseline.");
    if (uint64_t(enabled) * d.Repetitions * (d.WarmupFrames + d.MeasuredFrames + 64) > 60000)
        return fail("The experiment exceeds its 60000-frame budget.");
    error.clear(); return true;
}
std::string Serialize(const Definition& d)
{
    nlohmann::json j = {{"schema",d.Schema},{"name",d.Name},{"family",d.Family},{"baseline",d.Baseline},
        {"warmupFrames",d.WarmupFrames},{"measuredFrames",d.MeasuredFrames},{"repetitions",d.Repetitions},
        {"fixedStep",d.FixedStep},{"motionDegrees",d.MotionDegrees},{"scenario",uint32_t(d.Motion)},
        {"displayExposure",d.DisplayExposure},{"region",d.Region},{"common",d.Common}};
    j["variants"] = nlohmann::json::array();
    if(d.Camera){const auto& c=*d.Camera;j["camera"]={{"position",c.Position},{"target",c.Target},{"fovDegrees",c.FovDegrees},{"near",c.NearClip},{"far",c.FarClip},{"aspect",c.Aspect}};}
    else j["camera"]=nullptr;
    for (const auto& v : d.Variants) j["variants"].push_back({{"id",v.Id},{"label",v.Label},
        {"algorithm",v.AlgorithmId},{"enabled",v.Enabled},{"overrides",v.Overrides}});
    return j.dump(2) + '\n';
}
bool Parse(const std::string& text, Definition& output, std::string& error)
{
    try
    {
        if (text.size() > 65536) { error = "Experiment file exceeds 64 KiB."; return false; }
        const auto j = nlohmann::json::parse(text);
        Definition d;
        const auto integer = [&](const char* key) {
            const auto& value = j.at(key);
            if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > 1000000)
                throw std::runtime_error(std::string(key) + " must be a nonnegative bounded integer");
            return value.get<uint32_t>();
        };
        d.Schema=integer("schema"); d.Name=j.at("name"); d.Family=j.at("family"); d.Baseline=j.at("baseline");
        d.WarmupFrames=integer("warmupFrames"); d.MeasuredFrames=integer("measuredFrames"); d.Repetitions=integer("repetitions");
        d.Motion=Scenario(integer("scenario")); d.FixedStep=j.at("fixedStep"); d.MotionDegrees=j.at("motionDegrees");
        d.DisplayExposure=j.at("displayExposure"); d.Region=j.at("region").get<std::array<double,4>>();
        d.Common=j.at("common").get<Parameters>();
        if(j.contains("camera")&&!j.at("camera").is_null())
        {
            const auto& c=j.at("camera");CameraSettings camera;
            camera.Position=c.at("position").get<std::array<double,3>>();camera.Target=c.at("target").get<std::array<double,3>>();
            camera.FovDegrees=c.at("fovDegrees");camera.NearClip=c.at("near");camera.FarClip=c.at("far");camera.Aspect=c.at("aspect");
            d.Camera=camera;
        }
        if (!j.at("variants").is_array() || j.at("variants").size()>8) throw std::runtime_error("Too many variants");
        for (const auto& v : j.at("variants"))
            d.Variants.push_back({v.at("id"),v.at("label"),v.at("algorithm"),v.at("overrides").get<Parameters>(),v.at("enabled").get<bool>()});
        if (!Validate(d,error)) return false;
        output=std::move(d); error.clear(); return true;
    }
    catch (const std::exception& e) { error=e.what(); return false; }
}
std::vector<Task> MakePlan(const Definition& d)
{
    std::vector<Task> result;
    for (uint32_t repeat=0;repeat<d.Repetitions;++repeat)
    {
        std::vector<Task> round;
        for (size_t i=0;i<d.Variants.size();++i) if (d.Variants[i].Enabled) round.push_back({i,repeat});
        if (repeat % 2) std::reverse(round.begin(),round.end());
        result.insert(result.end(),round.begin(),round.end());
    }
    return result;
}
std::optional<Summary> Summarize(std::span<const double> values)
{
    if (values.empty()) return {};
    std::vector<double> sorted(values.begin(),values.end());
    for (double v : sorted) if (!std::isfinite(v) || v<0) return {};
    std::sort(sorted.begin(),sorted.end()); Summary s; s.Count=sorted.size();
    for (double v : sorted) s.Mean+=v/double(s.Count);
    for (double v : sorted) s.StandardDeviation+=(v-s.Mean)*(v-s.Mean)/double(s.Count);
    s.StandardDeviation=std::sqrt(s.StandardDeviation);
    const auto quantile = [&](double q) { double x=q*(sorted.size()-1);auto i=size_t(x);
        return sorted[i]+(sorted[std::min(i+1,sorted.size()-1)]-sorted[i])*(x-i); };
    s.Median=quantile(.5);s.P95=quantile(.95);s.Minimum=sorted.front();s.Maximum=sorted.back();return s;
}
bool Load(const std::filesystem::path& path,Definition& output,std::string& error)
{
    try
    {
        const auto size=std::filesystem::file_size(path);
        if(size>65536)throw std::runtime_error("Experiment file exceeds 64 KiB");
        std::ifstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);
        std::string text(size_t(size),'\0');file.read(text.data(),std::streamsize(size));
        return Parse(text,output,error);
    }
    catch(const std::exception& e){error=e.what();return false;}
}
bool Save(const std::filesystem::path& path,const Definition& definition,std::string& error)
{
    namespace fs=std::filesystem;fs::path temporary;
    try
    {
        if(!Validate(definition,error))return false;
        if(path.empty()||path.filename().empty())throw std::runtime_error("Choose an experiment filename");
        const auto parent=fs::absolute(path).parent_path();fs::create_directories(parent);
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned i=0;i<100;++i)
        {
            temporary=parent/(".lab-write-"+std::to_string(stamp)+"-"+std::to_string(i));
            if(fs::create_directory(temporary))break;
            temporary.clear();
        }
        if(temporary.empty())throw std::runtime_error("Could not reserve an atomic experiment save");
        const auto file=temporary/"experiment.jlab";
        {std::ofstream out(file,std::ios::binary);out.exceptions(std::ios::badbit|std::ios::failbit);out<<Serialize(definition);}
        fs::rename(file,fs::absolute(path));fs::remove(temporary);error.clear();return true;
    }
    catch(const std::exception& e)
    {
        if(!temporary.empty()){std::error_code ignored;fs::remove_all(temporary,ignored);}
        error=e.what();return false;
    }
}
Difference Compare(std::span<const float> a,std::span<const float> b,uint32_t width,uint32_t height,const std::array<double,4>& roi)
{
    Difference d; const uint64_t count=uint64_t(width)*height;
    if (!count || count>4194304 || a.size()!=count*4 || b.size()!=a.size() || !RegionValid(roi)) return d;
    const auto x0=uint32_t(std::floor(roi[0]*width)), y0=uint32_t(std::floor(roi[1]*height));
    const auto x1=std::min(width,uint32_t(std::ceil((roi[0]+roi[2])*width)));
    const auto y1=std::min(height,uint32_t(std::ceil((roi[1]+roi[3])*height)));
    double absolute=0,squared=0;
    for(uint32_t y=y0;y<y1;++y)for(uint32_t x=x0;x<x1;++x)
    {
        const auto i=(uint64_t(y)*width+x)*4; bool finite=true;
        for(size_t c=0;c<3;++c) finite &= std::isfinite(a[i+c]) && std::isfinite(b[i+c]);
        if(!finite){++d.NonFinitePixels;continue;}
        ++d.Pixels;
        for(size_t c=0;c<3;++c){const double delta=std::abs(double(a[i+c])-b[i+c]);absolute+=delta;squared+=delta*delta;d.Maximum=std::max(d.Maximum,delta);}
    }
    d.Valid=d.Pixels>0 && !d.NonFinitePixels;
    if(d.Valid){d.Mae=absolute/(d.Pixels*3);d.Rmse=std::sqrt(squared/(d.Pixels*3));}
    return d;
}
std::string EscapeHtml(const std::string& text)
{
    std::string out;
    for(char c:text) switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;
        case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:out+=c;}
    return out;
}
}
