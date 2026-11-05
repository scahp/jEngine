#include "jEnvironmentLighting.h"
#include "PathTracingDataLoader/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <stdexcept>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// Reference: scahp/Shadows (MIT), IrradianceMap@51b3ac66397580f586a50c8d882b2e1da794cdb0.
// This module replaces demo/GPU state with a synchronous, renderer-independent
// projection and explicitly records the coefficient/basis convention.
namespace jEnvironmentLighting
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double C0 = 0.28209479177387814347, C1 = 0.48860251190291992159;
constexpr double C2 = 1.09254843059207907054, C3 = 0.31539156525252000603;
constexpr double C4 = 0.54627421529603953527, MaximumCoefficient = 1e12;

bool ValidAssetPath(const std::filesystem::path& path)
{
    return !path.empty() && !path.filename().empty()
        && path.native().find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
}

bool Basis(const Direction& direction, std::array<double, 9>& out) noexcept
{
    const double x0 = direction[0], y0 = direction[1], z0 = direction[2];
    const double length = std::hypot(std::hypot(x0, y0), z0);
    if (!std::isfinite(length) || length <= 0.) return false;
    const double x = x0 / length, y = y0 / length, z = z0 / length;
    out = {C0, C1*y, C1*z, C1*x, C2*x*y, C2*y*z,
        C3*(3.*z*z-1.), C2*x*z, C4*(x*x-y*y)};
    return true;
}

RGB Evaluate(const Coefficients& coefficients, const Direction& direction, bool diffuse) noexcept
{
    std::array<double, 9> basis{};
    if (!Basis(direction, basis)) return {};
    std::array<double, 3> value{};
    for (std::size_t i = 0; i < coefficients.size(); ++i)
    {
        const double convolution = !diffuse ? 1. : i == 0 ? Pi : i < 4 ? 2.*Pi/3. : Pi/4.;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const double coefficient = coefficients[i][channel];
            if (!std::isfinite(coefficient) || std::abs(coefficient) > MaximumCoefficient) return {};
            value[channel] += coefficient * basis[i] * convolution;
        }
    }
    RGB out{};
    for (std::size_t channel = 0; channel < 3; ++channel)
        out[channel] = static_cast<float>(diffuse ? std::max(0., value[channel]) : value[channel]);
    return out;
}

bool Cancelled(const ProjectionOptions& options, std::string& error)
{
    if (options.Cancelled && options.Cancelled())
    { error = "Environment projection cancelled."; return true; }
    return false;
}

struct StagingFile
{
    std::filesystem::path Path;
    FILE* Stream = nullptr;
    ~StagingFile()
    {
        if (Stream) std::fclose(Stream);
        if (!Path.empty()) { std::error_code ignored; std::filesystem::remove(Path, ignored); }
    }
};

bool WriteAtomic(const std::filesystem::path& path, const std::string& text, std::string& error)
{
    if (!ValidAssetPath(path))
    { error = "An environment asset file path is required."; return false; }
    std::error_code ec;
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    if (!std::filesystem::is_directory(parent, ec))
    { error = "Environment asset parent directory does not exist."; return false; }
    const auto status = std::filesystem::symlink_status(path, ec);
    if (!ec && std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))
    { error = "Environment asset destination must be a regular file."; return false; }
    if (ec && ec != std::errc::no_such_file_or_directory)
    { error = "Cannot inspect environment asset destination: " + ec.message(); return false; }
    static std::atomic<std::uint64_t> sequence{0};
    StagingFile staging;
    int descriptor = -1;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        staging.Path = path;
        staging.Path += ".tmp-" + std::to_string(token) + "-" + std::to_string(sequence.fetch_add(1));
#ifdef _WIN32
        if (_wsopen_s(&descriptor, staging.Path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY | _O_NOINHERIT,
            _SH_DENYRW, _S_IREAD | _S_IWRITE) == 0) break;
#else
        descriptor = ::open(staging.Path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (descriptor >= 0) break;
#endif
        staging.Path.clear(); // An existing file at a colliding name is not ours.
    }
    if (descriptor < 0) { error = "Cannot create environment asset staging file."; return false; }
#ifdef _WIN32
    staging.Stream = _fdopen(descriptor, "wb");
#else
    staging.Stream = fdopen(descriptor, "wb");
#endif
    if (!staging.Stream)
    {
#ifdef _WIN32
        _close(descriptor);
#else
        ::close(descriptor);
#endif
        error = "Cannot open environment asset staging stream."; return false;
    }
    bool complete = std::fwrite(text.data(), 1, text.size(), staging.Stream) == text.size()
        && std::fflush(staging.Stream) == 0;
#ifdef _WIN32
    complete = complete && _commit(descriptor) == 0;
#else
    complete = complete && ::fsync(descriptor) == 0;
#endif
    const int closeResult = std::fclose(staging.Stream); staging.Stream = nullptr;
    if (!complete || closeResult != 0)
    { error = "Cannot complete environment asset staging write."; return false; }
    // Same-directory rename has atomic replacement semantics; failure leaves
    // the old destination intact, with only this call's staging file cleaned up.
    std::filesystem::rename(staging.Path, path, ec);
    if (ec) { error = "Cannot publish environment asset: " + ec.message(); return false; }
    staging.Path.clear();
    return true;
}
}

bool Validate(const Coefficients& coefficients, std::string& error)
{
    error.clear();
    for (const auto& rgb : coefficients)
        for (const double component : rgb)
            if (!std::isfinite(component) || std::abs(component) > MaximumCoefficient)
            { error = "Environment SH coefficients must be finite and bounded to +/-1e12."; return false; }
    return true;
}

bool ProjectEquirectangular(const ImageView& image, Coefficients& out, std::string& error,
    const ProjectionOptions& options)
{
    error.clear();
    try
    {
        const std::uint64_t count = std::uint64_t(image.Width) * image.Height;
        if (!image.Width || !image.Height || image.Width > 16384 || image.Height > 16384 || !image.Pixels
            || count != image.PixelCount || !options.MaxTexels || options.MaxTexels > MaximumProjectionTexels
            || count > options.MaxTexels)
        { error = "Invalid or over-budget environment image dimensions/pixel count."; return false; }
        if (Cancelled(options, error)) return false;
        // Integrate each real SH basis exactly over a constant-radiance texel.
        // Unlike center-only solid-angle quadrature this preserves a constant
        // HDR environment exactly, including very low input resolutions.
        struct PhiIntegral { double P, Cos, Sin, Cos2, Sin2, SinCos; };
        std::vector<PhiIntegral> columns(image.Width);
        for (std::uint32_t x = 0; x < image.Width; ++x)
        {
            const double p0 = 2.*Pi*x/image.Width, p1 = 2.*Pi*(x+1.)/image.Width, delta = p1-p0;
            const double second = .25*(std::sin(2.*p1)-std::sin(2.*p0));
            columns[x] = {delta, std::sin(p1)-std::sin(p0), std::cos(p0)-std::cos(p1),
                .5*delta+second, .5*delta-second, .25*(std::cos(2.*p0)-std::cos(2.*p1))};
        }
        std::array<std::array<double, 3>, 9> accumulated{};
        for (std::uint32_t y = 0; y < image.Height; ++y)
        {
            if (Cancelled(options, error)) return false;
            const double t0 = Pi*y/image.Height, t1 = Pi*(y+1.)/image.Height;
            const double c0 = std::cos(t0), c1 = std::cos(t1), s0 = std::sin(t0), s1 = std::sin(t1);
            const double solid = c0-c1, sin2 = .5*(t1-t0)-.25*(std::sin(2.*t1)-std::sin(2.*t0));
            const double cosSin = .5*(s1*s1-s0*s0), cos2Sin = (c0*c0*c0-c1*c1*c1)/3.;
            const double sin3 = solid-cos2Sin, sin2Cos = (s1*s1*s1-s0*s0*s0)/3.;
            for (std::uint32_t x = 0; x < image.Width; ++x)
            {
                if ((x & 255u) == 0 && Cancelled(options, error)) return false;
                const auto& p = columns[x];
                const std::array<double, 9> integrated = {C0*solid*p.P, C1*cosSin*p.P,
                    C1*sin2*p.Sin, C1*sin2*p.Cos, C2*sin2Cos*p.Cos, C2*sin2Cos*p.Sin,
                    C3*(3.*sin3*p.Sin2-solid*p.P), C2*sin3*p.SinCos, C4*(sin3*p.Cos2-cos2Sin*p.P)};
                const auto& rgb = image.Pixels[std::size_t(y)*image.Width+x];
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    if (!std::isfinite(rgb[channel]) || rgb[channel] < 0.f || rgb[channel] > 1e10f)
                    { error = "Environment image must contain finite linear radiance in [0,1e10]."; return false; }
                    for (std::size_t i = 0; i < integrated.size(); ++i)
                        accumulated[i][channel] += rgb[channel]*integrated[i];
                }
            }
        }
        Coefficients candidate{};
        for (std::size_t i = 0; i < candidate.size(); ++i)
            for (std::size_t channel = 0; channel < 3; ++channel)
                candidate[i][channel] = static_cast<float>(accumulated[i][channel]);
        if (!Validate(candidate, error) || Cancelled(options, error)) return false;
        out = candidate; return true;
    }
    catch (const std::exception& exception)
    { error = std::string("Environment projection failed: ") + exception.what(); return false; }
}

RGB EvaluateRadiance(const Coefficients& coefficients, const Direction& direction) noexcept
{ return Evaluate(coefficients, direction, false); }
RGB EvaluateDiffuseIrradiance(const Coefficients& coefficients, const Direction& normal) noexcept
{ return Evaluate(coefficients, normal, true); }

bool LoadAsset(const std::filesystem::path& path, Coefficients& out, std::string& error)
{
    error.clear();
    try
    {
        if (!ValidAssetPath(path)) { error = "An environment asset file path without NUL is required."; return false; }
        std::ifstream file(path, std::ios::binary);
        if (!file) { error = "Cannot open environment SH asset."; return false; }
        std::string text(MaximumAssetBytes+1, '\0');
        file.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(file.gcount()));
        if (file.bad() || text.empty() || text.size() > MaximumAssetBytes)
        { error = "Environment SH asset is empty, unreadable or larger than 64 KiB."; return false; }
        std::set<std::string> keys;
        auto json = nlohmann::json::parse(text, [&keys](int depth, nlohmann::json::parse_event_t event, nlohmann::json& parsed)
        {
            if (depth > 3) throw std::invalid_argument("Environment asset nesting limit exceeded.");
            if (event == nlohmann::json::parse_event_t::key && !keys.insert(parsed.get<std::string>()).second)
                throw std::invalid_argument("Duplicate environment asset field.");
            return true;
        });
        if (!json.is_object() || json.size() != 6 || json.at("format") != "jEngine.EnvironmentSH"
            || !json.at("version").is_number_integer() || json.at("version") != AssetVersion
            || json.at("space") != "world-y-up" || json.at("basis") != "real-sh-l2-sloan-xyz"
            || json.at("units") != "linear-radiance" || !json.at("coefficients").is_array()
            || json.at("coefficients").size() != 9)
        { error = "Unsupported environment SH asset version, layout, coordinate space or units."; return false; }
        Coefficients candidate{};
        for (std::size_t i = 0; i < candidate.size(); ++i)
        {
            const auto& rgb = json.at("coefficients").at(i);
            if (!rgb.is_array() || rgb.size() != 3)
            { error = "Environment SH asset requires exactly 9 RGB coefficients."; return false; }
            for (std::size_t c = 0; c < 3; ++c)
            {
                if (!rgb[c].is_number()) { error = "Environment coefficient is not numeric."; return false; }
                const double value = rgb[c].get<double>();
                if (!std::isfinite(value) || std::abs(value) > MaximumCoefficient)
                { error = "Environment coefficient is nonfinite or out of bounds."; return false; }
                candidate[i][c] = static_cast<float>(value);
            }
        }
        if (!Validate(candidate, error)) return false;
        out = candidate; return true;
    }
    catch (const std::exception& exception)
    { error = std::string("Cannot load environment SH asset: ") + exception.what(); return false; }
}

bool SaveAsset(const std::filesystem::path& path, const Coefficients& coefficients, std::string& error)
{
    if (!Validate(coefficients, error)) return false;
    try
    {
        nlohmann::json json = {{"format", "jEngine.EnvironmentSH"}, {"version", AssetVersion},
            {"space", "world-y-up"}, {"basis", "real-sh-l2-sloan-xyz"}, {"units", "linear-radiance"},
            {"coefficients", coefficients}};
        const std::string text = json.dump(2) + "\n";
        if (text.size() > MaximumAssetBytes) { error = "Environment SH asset exceeds size limit."; return false; }
        return WriteAtomic(path, text, error);
    }
    catch (const std::exception& exception)
    { error = std::string("Cannot save environment SH asset: ") + exception.what(); return false; }
}
}
