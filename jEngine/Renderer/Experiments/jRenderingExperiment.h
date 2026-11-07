#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace jRenderingExperiment
{
using Parameters = std::map<std::string, double>;
enum class Scenario : uint32_t { Static, CameraArc, LightSweep };
struct Algorithm { const char* Id; const char* Name; const char* Family; };
std::span<const Algorithm> Algorithms();
const Algorithm* FindAlgorithm(const std::string& id);
struct Variant
{
    std::string Id, Label, AlgorithmId;
    Parameters Overrides;
    bool Enabled = true;
};
struct CameraSettings
{
    std::array<double,3> Position{0,80,-180}, Target{0,10,0};
    double FovDegrees = 60, NearClip = 1, FarClip = 5000, Aspect = 0;
};
struct Definition
{
    uint32_t Schema = 1;
    std::string Name = "Shadow comparison", Family = "shadows", Baseline = "pcf";
    uint32_t WarmupFrames = 30, MeasuredFrames = 60, Repetitions = 1;
    double FixedStep = 1.0 / 60.0, MotionDegrees = 15, DisplayExposure = 0;
    Scenario Motion = Scenario::Static;
    std::array<double, 4> Region{0, 0, 1, 1}; // normalized x/y/width/height
    Parameters Common;
    std::vector<Variant> Variants;
    std::optional<CameraSettings> Camera;
};
Definition ShadowDefaults();
Parameters ResolvedParameters(const Definition& definition, const Variant& variant);
bool Validate(const Definition& definition, std::string& error);
std::string Serialize(const Definition& definition);
bool Parse(const std::string& text, Definition& output, std::string& error);
bool Load(const std::filesystem::path& path, Definition& output, std::string& error);
bool Save(const std::filesystem::path& path, const Definition& definition, std::string& error);
struct Task { size_t VariantIndex = 0; uint32_t Repetition = 0; };
// Alternate ordering across repetitions to reduce fixed-order cache/thermal bias.
std::vector<Task> MakePlan(const Definition& definition);
struct Summary
{
    size_t Count = 0;
    double Mean = 0, Median = 0, P95 = 0, Minimum = 0, Maximum = 0, StandardDeviation = 0;
};
std::optional<Summary> Summarize(std::span<const double> values);
struct Difference
{
    bool Valid = false;
    uint64_t Pixels = 0, NonFinitePixels = 0;
    double Mae = 0, Rmse = 0, Maximum = 0;
};
// Linear RGB only. Alpha does not contribute; a baseline is not a ground truth.
Difference Compare(std::span<const float> baseline, std::span<const float> candidate,
    uint32_t width, uint32_t height, const std::array<double, 4>& region);
std::string EscapeHtml(const std::string& text);
}
