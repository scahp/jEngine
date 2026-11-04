#ifndef JENGINE_PACKAGING_STANDALONE
#include "pch.h"
#endif
#include "jEditorPackaging.h"
#include "External/PathTracingDataLoader/json.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace jEditorPackaging
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string Text(const fs::path& path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
fs::path Path(const std::string& text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
std::string Environment(const char* name)
{
#ifdef _WIN32
    const std::wstring wide(name, name + std::char_traits<char>::length(name));
    const DWORD length = GetEnvironmentVariableW(wide.c_str(), nullptr, 0);
    if (!length) return {};
    std::wstring value(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(wide.c_str(), value.data(), length);
    if (!copied || copied >= length) return {};
    value.resize(copied); return Text(fs::path(value));
#else
    const char* value = std::getenv(name); return value ? value : "";
#endif
}
std::string Lower(std::string value)
{
    for (auto& byte : value)
        if (byte >= 'A' && byte <= 'Z') byte = char(byte - 'A' + 'a');
    return value;
}
void CheckPath(const fs::path& path)
{
    const auto text = Text(path);
    if (text.empty() || text.find_first_of(";\r\n") != std::string::npos)
        throw std::runtime_error("Packaging paths must be nonempty and cannot contain semicolons/newlines: " + text);
}
fs::path File(const fs::path& path)
{
    CheckPath(path);
    std::error_code error;
    const auto result = fs::canonical(path, error);
    if (error || !fs::is_regular_file(result))
        throw std::runtime_error("Required runtime file is missing/unreadable: " + Text(path)
            + (error ? ": " + error.message() : std::string()));
    return result;
}
fs::path FindTool(const std::string& name)
{
    const auto environment = Environment("PATH");
#ifdef _WIN32
    constexpr char separator = ';';
    const std::string filename = name + ".exe";
#else
    constexpr char separator = ':';
    const auto& filename = name;
#endif
    size_t at = 0;
    while (at <= environment.size())
    {
        const auto end = environment.find(separator, at);
        const auto segment = environment.substr(at, end == std::string::npos ? end : end - at);
        if (!segment.empty())
        {
            const auto candidate = Path(segment) / filename;
            std::error_code error;
            if (fs::is_regular_file(candidate, error)) return File(candidate);
        }
        if (end == std::string::npos) break;
        at = end + 1;
    }
    throw std::runtime_error("Cannot find " + name + " on PATH. Install CMake 3.25+ and expose its bin directory to the editor before packaging.");
}
struct Temporary
{
    fs::path Directory;
    Temporary()
    {
        for (unsigned attempt = 0; attempt != 64; ++attempt)
        {
            const auto candidate = fs::temp_directory_path() / ("jengine-package-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate))
            {
                Directory = candidate;
                fs::permissions(Directory, fs::perms::owner_all, fs::perm_options::replace);
                return;
            }
        }
        throw std::runtime_error("Cannot reserve a private runtime-packaging temporary directory");
    }
    ~Temporary() { std::error_code ignored; if (!Directory.empty()) fs::remove_all(Directory, ignored); }
};
void Write(const fs::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary); out.write(text.data(), static_cast<std::streamsize>(text.size())); out.close();
    if (!out) throw std::runtime_error("Cannot write generated packaging input: " + Text(file));
}
Json Read(const fs::path& file)
{
    if (fs::file_size(file) > 4 * 1024 * 1024) throw std::runtime_error("Runtime dependency metadata exceeds 4 MiB: " + Text(file));
    std::ifstream input(file, std::ios::binary); Json result; input >> result;
    if (!input && !input.eof()) throw std::runtime_error("Cannot read runtime dependency metadata: " + Text(file));
    return result;
}
std::string Diagnostic(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    in.seekg(0, std::ios::end); const auto length = in.tellg();
    if (length > 16384) in.seekg(length - std::streamoff(16384)); else in.seekg(0);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
#ifdef _WIN32
std::wstring Wide(const std::string& text)
{
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 runtime tool argument");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
std::wstring Quote(const std::wstring& argument)
{
    // Windows CRT argv quoting; no cmd.exe, PowerShell or shell interpolation.
    std::wstring result = L"\""; size_t slashes = 0;
    for (const wchar_t value : argument)
    {
        if (value == L'\\') { ++slashes; continue; }
        result.append(slashes * (value == L'\"' ? 2 : 1), L'\\'); slashes = 0;
        if (value == L'\"') result += L'\\';
        result += value;
    }
    result.append(slashes * 2, L'\\'); result += L'\"'; return result;
}
#endif
bool Run(const std::vector<std::string>& arguments, const fs::path& log, std::string& error)
{
    if (arguments.empty()) { error = "Missing runtime-packaging tool"; return false; }
    bool success = false;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) { error = "Cannot create runtime tool diagnostic log"; return false; }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) { CloseHandle(output); error = "Cannot create runtime tool input handle"; return false; }
    std::wstring command;
    for (const auto& argument : arguments) { if (!command.empty()) command += L' '; command += Quote(Wide(argument)); }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output; startup.hStdError = output; startup.hStdInput = input;
    PROCESS_INFORMATION process{};
    const auto executable = Wide(arguments[0]);
    const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(output); CloseHandle(input);
    if (!created) { error = "Cannot start CMake packaging tool (Windows error " + std::to_string(createError) + ")"; return false; }
    const DWORD wait = WaitForSingleObject(process.hProcess, 180000);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, INFINITE);
        error = wait == WAIT_TIMEOUT ? "Runtime dependency inspection timed out after 180 seconds"
            : "Cannot wait for the CMake runtime-packaging process";
    }
    DWORD exitCode = 1; GetExitCodeProcess(process.hProcess, &exitCode);
    success = wait == WAIT_OBJECT_0 && exitCode == 0;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
#else
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    int setup = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (!setup) setup = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    posix_spawnattr_t attributes; posix_spawnattr_init(&attributes);
    if (!setup) setup = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (!setup) setup = posix_spawnattr_setpgroup(&attributes, 0);
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr); pid_t child = 0;
    const int spawn = setup ? setup : posix_spawn(&child, arguments[0].c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
    if (spawn) { error = "Cannot start CMake runtime-packaging tool (errno " + std::to_string(spawn) + ")"; return false; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    int status = 0;
    for (;;)
    {
        const pid_t completed = waitpid(child, &status, WNOHANG);
        if (completed == child) { success = WIFEXITED(status) && WEXITSTATUS(status) == 0; break; }
        if (completed < 0 && errno != EINTR) { error = "Cannot wait for runtime-packaging tool"; break; }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            kill(-child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            error = "Runtime dependency inspection timed out after 180 seconds"; break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
    if (!success)
    {
        if (error.empty()) error = "CMake runtime dependency collection/fixup failed";
        error += "\n" + Diagnostic(log);
    }
    return success;
}

struct Plan
{
    std::shared_ptr<Temporary> TemporaryFiles = std::make_shared<Temporary>();
    fs::path CMake, Script, Description;
    Json Data = {{"executables", Json::array()}, {"modules", Json::array()}, {"directories", Json::array()},
        {"executableDestinations", Json::array()}, {"moduleDestinations", Json::array()}, {"launchers", Json::array()}, {"libraries", Json::array()}};
};
fs::path Script(const fs::path& resource)
{
    const std::vector<fs::path> candidates{
        resource.parent_path().parent_path() / "scripts/package-runtime.cmake",
        fs::absolute(fs::path(__FILE__)).parent_path() / "../../../../scripts/package-runtime.cmake"};
    for (const auto& candidate : candidates)
        if (fs::is_regular_file(candidate)) return File(candidate);
    throw std::runtime_error("Cannot find scripts/package-runtime.cmake. Package from a complete editor checkout with its Resource directory.");
}
void AddMapping(std::vector<jEditorRuntimeFile>& files, const fs::path& source, const std::string& destination)
{
    CheckPath(source);
    if (destination.empty() || fs::path(destination).is_absolute() || destination.find_first_of(";\r\n\\") != std::string::npos)
        throw std::runtime_error("Invalid package-relative runtime destination: " + destination);
    for (const auto& part : fs::path(destination)) if (part == ".." || part == ".")
        throw std::runtime_error("Unsafe package-relative runtime destination: " + destination);
    const auto canonical = fs::canonical(source);
    for (const auto& mapping : files)
        if (Lower(mapping.Destination) == Lower(destination))
        {
            if (fs::equivalent(mapping.Source, canonical)) return;
            throw std::runtime_error("Two different runtime inputs use " + destination + ": " + Text(mapping.Source) + " / " + Text(source));
        }
    files.push_back({canonical, destination});
}
void AddLibrary(Plan& plan, std::vector<jEditorRuntimeFile>& files, const fs::path& library,
    const std::string& requestedDestination = {})
{
    const auto source = File(library);
    if (Text(source).find(".framework/") != std::string::npos)
        throw std::runtime_error("Non-system framework needs an explicit complete runtime bundle mapping: " + Text(source));
    const auto alias = Text(library.filename());
    for (auto& existing : plan.Data["libraries"])
        if (existing.at("source").get<std::string>() == Text(source))
        {
            auto& aliases = existing["aliases"];
            if (std::find(aliases.begin(), aliases.end(), Json(alias)) == aliases.end()) aliases.push_back(alias);
            return;
        }
#ifdef __APPLE__
    const auto destination = requestedDestination.empty() ? "runtime/lib/" + alias : requestedDestination;
#else
    const auto destination = requestedDestination.empty() ? "runtime/" + alias : requestedDestination;
#endif
    AddMapping(files, source, destination);
    plan.Data["libraries"].push_back({{"source", Text(source)}, {"destination", destination},
        {"aliases", {alias, Text(source.filename()), Text(fs::path(destination).filename())}}});
}

#ifdef __APPLE__
fs::path FindDxc(const fs::path& resource)
{
    auto configured = Environment("JENGINE_DXC_EXECUTABLE");
    if (!configured.empty()) return File(Path(configured));
#ifdef JENGINE_DXC_EXECUTABLE
    if (fs::is_regular_file(Path(JENGINE_DXC_EXECUTABLE))) return File(Path(JENGINE_DXC_EXECUTABLE));
#endif
    const auto repository = resource.parent_path().parent_path();
    for (const auto& path : {repository / ".ci-tools/dxc/bin/dxc",
        repository.parent_path() / "DirectXShaderCompiler/build/macos/bin/dxc"})
        if (fs::is_regular_file(path)) return File(path);
    return FindTool("dxc");
}
std::pair<Json, bool> Driver(const fs::path& resource)
{
    const auto configured = Environment("VK_DRIVER_FILES").empty()
        ? Environment("VK_ICD_FILENAMES") : Environment("VK_DRIVER_FILES");
    fs::path manifest; bool experimental = Environment("MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING") == "1";
    if (!configured.empty())
    {
        if (configured.find(':') != std::string::npos || configured.find(';') != std::string::npos)
            throw std::runtime_error("Packaging requires one explicit MoltenVK ICD in VK_DRIVER_FILES, not an ICD list");
        manifest = File(Path(configured));
    }
    else
    {
        const auto verified = resource.parent_path().parent_path() / "build/macos-rayquery/verified-runtime.json";
        if (fs::is_regular_file(verified))
        {
            const auto value = Read(verified);
            manifest = File(Path(value.at("icd").get<std::string>())); experimental = true;
        }
        else for (const auto* candidate : {"/opt/homebrew/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json",
            "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json", "/usr/local/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json"})
            if (fs::is_regular_file(candidate)) { manifest = File(candidate); break; }
    }
    if (manifest.empty()) throw std::runtime_error("Select a working MoltenVK ICD with VK_DRIVER_FILES before packaging");
    auto json = Read(manifest);
    const auto name = Path(json.at("ICD").at("library_path").get<std::string>());
    const auto library = File(name.is_absolute() ? name : manifest.parent_path() / name);
    json["ICD"]["library_path"] = Text(library); // Internal collection input; rewritten before shipping.
    return {std::move(json), experimental};
}
#endif
}

bool PrepareRuntime(const fs::path& binaryPath, const fs::path& resourceRoot,
    std::vector<jEditorRuntimeFile>& files, std::string& runtimeExecutable,
    FinalizeFunction& finalize, std::string& error)
{
    error.clear();
    try
    {
#if !defined(__APPLE__) && !defined(_WIN32)
        throw std::runtime_error("Portable runtime packaging supports Windows and macOS only");
#endif
        const auto binary = File(binaryPath);
        const auto resource = fs::canonical(resourceRoot); CheckPath(resource);
        if (!fs::is_directory(resource / "Shaders")) throw std::runtime_error("Required Resource/Shaders directory is missing: " + Text(resource));
        auto plan = std::make_shared<Plan>();
        plan->CMake = FindTool("cmake"); plan->Script = Script(resource);
        plan->Description = plan->TemporaryFiles->Directory / "runtime-plan.json";
        std::vector<jEditorRuntimeFile> prepared = files;
#ifdef _WIN32
        const std::string executable = "runtime/jEngine.exe";
#else
        const std::string executable = "runtime/jEngine";
#endif
        AddMapping(prepared, binary, executable);
        plan->Data["executables"].push_back(Text(binary)); plan->Data["executableDestinations"].push_back(executable);
        plan->Data["directories"].push_back(Text(binary.parent_path()));
        AddMapping(prepared, resource / "Shaders", "Resource/Shaders");
        for (const auto* name : {"stpeters_probe_cubemp.dds", "stpeters_probe_irradiancemap.dds", "stpeters_probe_filteredenvmap.dds"})
            AddMapping(prepared, File(resource / name), std::string("Resource/") + name);

        // Callers can explicitly seed native plugin inputs before this call.
        // Include their transitive closure as well as the engine import table.
        for (const auto& input : files)
        {
            const auto extension = Lower(Text(input.Source.extension()));
#ifdef _WIN32
            const bool module = extension == ".dll";
#else
            const bool module = extension == ".dylib" || extension == ".so";
#endif
            if (module)
            {
                const auto source = File(input.Source);
                AddLibrary(*plan, prepared, source, input.Destination);
                plan->Data["modules"].push_back(Text(source));
                plan->Data["moduleDestinations"].push_back(input.Destination);
                plan->Data["directories"].push_back(Text(source.parent_path()));
            }
        }

#ifdef __APPLE__
        const auto dxc = FindDxc(resource);
        AddMapping(prepared, dxc, "runtime/dxc/bin/dxc");
        plan->Data["executables"].push_back(Text(dxc)); plan->Data["executableDestinations"].push_back("runtime/dxc/bin/dxc");
        plan->Data["directories"].push_back(Text(dxc.parent_path().parent_path() / "lib"));
        auto [icd, experimental] = Driver(resource);
        const auto driver = Path(icd.at("ICD").at("library_path").get<std::string>());
        AddLibrary(*plan, prepared, driver, "runtime/lib/libMoltenVK.dylib");
        plan->Data["modules"].push_back(Text(driver)); plan->Data["moduleDestinations"].push_back("runtime/lib/libMoltenVK.dylib");
        plan->Data["directories"].push_back(Text(driver.parent_path()));
        icd["ICD"]["library_path"] = "lib/libMoltenVK.dylib";
        const auto icdFile = plan->TemporaryFiles->Directory / "MoltenVK_icd.json"; Write(icdFile, icd.dump(2) + "\n");
        AddMapping(prepared, icdFile, "runtime/MoltenVK_icd.json");
        const auto launcher = plan->TemporaryFiles->Directory / "run-jEngine.command";
        Write(launcher, "#!/bin/sh\nset -eu\npackage_root=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\ncd \"$package_root\"\n"
            "export VK_DRIVER_FILES=\"$package_root/runtime/MoltenVK_icd.json\"\nunset VK_ICD_FILENAMES\n"
            "export JENGINE_DXC_EXECUTABLE=\"$package_root/runtime/dxc/bin/dxc\"\n"
            + std::string(experimental ? "export MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING=1\n" : "unset MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING\n")
            + "exec \"$package_root/runtime/jEngine\" --runtime \"$package_root/jengine-runtime.json\" \"$@\"\n");
        AddMapping(prepared, launcher, "run-jEngine.command"); plan->Data["launchers"].push_back("run-jEngine.command");
#elif defined(_WIN32)
        // Include delay-loaded/plugin DLLs too: import-table inspection alone does
        // not see LoadLibrary("dxcompiler.dll") or the Agility SDK directory.
        for (const auto& required : {"dxcompiler.dll", "dxil.dll"}) File(binary.parent_path() / required);
        for (const auto& directory : {binary.parent_path(), binary.parent_path() / "D3D12"})
        {
            if (!fs::is_directory(directory)) continue;
            plan->Data["directories"].push_back(Text(directory));
            for (const auto& entry : fs::directory_iterator(directory))
                if (Lower(Text(entry.path().extension())) == ".dll" && fs::is_regular_file(entry.path()))
                {
                    const auto destination = "runtime/" + Text(entry.path().lexically_relative(binary.parent_path()));
                    AddLibrary(*plan, prepared, entry.path(), destination);
                    plan->Data["modules"].push_back(Text(File(entry.path())));
                    plan->Data["moduleDestinations"].push_back(destination);
                }
        }
        const auto launcher = plan->TemporaryFiles->Directory / "run-jEngine.cmd";
        Write(launcher, "@echo off\r\nsetlocal\r\npushd \"%~dp0\"\r\nif errorlevel 1 exit /b 1\r\n"
            "\"runtime\\jEngine.exe\" --runtime \"jengine-runtime.json\" %*\r\nset \"jengine_exit=%errorlevel%\"\r\npopd\r\nexit /b %jengine_exit%\r\n");
        AddMapping(prepared, launcher, "run-jEngine.cmd"); plan->Data["launchers"].push_back("run-jEngine.cmd");
#endif
        Write(plan->Description, plan->Data.dump(2));
        const auto result = plan->TemporaryFiles->Directory / "dependencies.json";
        if (!Run({Text(plan->CMake), "-DJENGINE_PACKAGE_MODE=collect", "-DJENGINE_PACKAGE_PLAN=" + Text(plan->Description),
            "-DJENGINE_PACKAGE_RESULT=" + Text(result), "-P", Text(plan->Script)}, plan->TemporaryFiles->Directory / "collect.log", error)) return false;
        const auto dependencies = Read(result);
        for (const auto& dependency : dependencies.at("resolved")) AddLibrary(*plan, prepared, Path(dependency.get<std::string>()));
        Write(plan->Description, plan->Data.dump(2));
        FinalizeFunction complete = [plan](const fs::path& stagedRoot, std::string& failure)
        {
            failure.clear();
            try
            {
                CheckPath(stagedRoot);
                return Run({Text(plan->CMake), "-DJENGINE_PACKAGE_MODE=finalize", "-DJENGINE_PACKAGE_PLAN=" + Text(plan->Description),
                    "-DJENGINE_PACKAGE_STAGE=" + Text(fs::absolute(stagedRoot)), "-P", Text(plan->Script)},
                    plan->TemporaryFiles->Directory / "finalize.log", failure);
            }
            catch (const std::exception& exception) { failure = exception.what(); return false; }
        };
        files.swap(prepared); runtimeExecutable = executable; finalize = std::move(complete); return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
}
