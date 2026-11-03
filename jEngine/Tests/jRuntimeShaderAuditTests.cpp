#include "pch.h"

#ifdef _WIN32
#include "RHI/DX12/jShaderCompiler_DX12.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <regex>
#include <winver.h>
#pragma comment(lib, "version.lib")

int ExportSurfelGIShaderFixtures(const char* directory, bool vulkan);

namespace
{
    bool RuntimeShaderFilesMatch(const std::filesystem::path& actual, const std::filesystem::path& expected)
    {
        const auto actualSize = std::filesystem::file_size(actual);
        if (!actualSize || actualSize != std::filesystem::file_size(expected)) return false;
        std::ifstream actualFile(actual, std::ios::binary);
        std::ifstream expectedFile(expected, std::ios::binary);
        if (!actualFile || !expectedFile)
            throw std::runtime_error("Cannot open runtime/package DLLs for comparison: "
                + actual.string() + " / " + expected.string());
        std::array<char, 65536> actualBytes{}, expectedBytes{};
        std::uintmax_t compared = 0;
        for (;;)
        {
            actualFile.read(actualBytes.data(), static_cast<std::streamsize>(actualBytes.size()));
            expectedFile.read(expectedBytes.data(), static_cast<std::streamsize>(expectedBytes.size()));
            if (actualFile.bad() || expectedFile.bad()
                || (actualFile.fail() && !actualFile.eof()) || (expectedFile.fail() && !expectedFile.eof()))
                throw std::runtime_error("Cannot read runtime/package DLLs for comparison: "
                    + actual.string() + " / " + expected.string());
            const auto actualCount = actualFile.gcount();
            if (actualCount != expectedFile.gcount()
                || std::memcmp(actualBytes.data(), expectedBytes.data(), static_cast<size_t>(actualCount)) != 0)
                return false;
            compared += static_cast<std::uintmax_t>(actualCount);
            if (actualFile.eof() || expectedFile.eof())
                return actualFile.eof() && expectedFile.eof() && compared == actualSize;
        }
    }

    std::string FlattenRuntimeShader(const std::filesystem::path& path,
        const std::filesystem::path& includeDirectory, std::set<std::string>& included)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read runtime shader fixture: " + path.string());
        std::string text((std::istreambuf_iterator<char>(file)), {});
        if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
        std::istringstream lines(text);
        std::ostringstream flattened;
        static const std::regex includePattern(R"include(^[ \t]*#include[ \t]+"([^"]+)")include");
        std::string line;
        while (std::getline(lines, line))
        {
            std::smatch match;
            if (!std::regex_search(line, match, includePattern))
            {
                flattened << line << '\n';
                continue;
            }
            const auto includePath = std::filesystem::canonical(includeDirectory / match[1].str());
            std::string key = includePath.string();
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            // The engine's shader loader expands each quoted include once.
            if (included.insert(key).second)
                flattened << FlattenRuntimeShader(includePath, includeDirectory, included);
        }
        return flattened.str();
    }

    void PrintRuntimeCompilerDiagnostics(const std::string& source, const wchar_t* entry,
        std::vector<const wchar_t*> options)
    {
        // The production wrapper reports compiler diagnostics only to the Windows
        // debugger. Repeat a failed invocation through the same DLL for CI logs.
        jDXC library;
        ComPtr<IDxcCompiler> compiler;
        ComPtr<IDxcLibrary> utilities;
        ComPtr<IDxcBlobEncoding> text;
        ComPtr<IDxcOperationResult> result;
        if (FAILED(library.Initialize())
            || FAILED(library.CreateInstance(CLSID_DxcCompiler, compiler.GetAddressOf()))
            || FAILED(library.CreateInstance(CLSID_DxcLibrary, utilities.GetAddressOf()))
            || FAILED(utilities->CreateBlobWithEncodingFromPinned(
                reinterpret_cast<LPBYTE>(const_cast<char*>(source.data())), static_cast<UINT32>(source.size()), 0, &text)))
        {
            std::cerr << "Runtime DXC diagnostic initialization failed" << std::endl;
            return;
        }
#if _DEBUG
        options.insert(options.begin(), { L"-Zi", L"-Qembed_debug", L"-Od" });
#else
        options.insert(options.begin(), L"-O3");
#endif
        const HRESULT invocation = compiler->Compile(text.Get(), nullptr, entry, L"cs_6_6",
            options.data(), static_cast<UINT32>(options.size()), nullptr, 0, nullptr, &result);
        if (FAILED(invocation) || !result)
        {
            std::cerr << "Runtime DXC Compile HRESULT=0x" << std::hex << invocation << std::dec << std::endl;
            return;
        }
        ComPtr<IDxcBlobEncoding> errors;
        if (SUCCEEDED(result->GetErrorBuffer(&errors)) && errors && errors->GetBufferSize())
            std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        HRESULT status = E_FAIL;
        result->GetStatus(&status);
        std::cerr << "Runtime DXC compilation status=0x" << std::hex << status << std::dec << std::endl;
    }
}
#endif

int RunRuntimeShaderAuditTests()
{
#ifndef _WIN32
    return 0;
#else
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
        return condition;
    };
    auto& compiler = jShaderCompiler_DX12::Get();
    if (!expect(SUCCEEDED(compiler.Initialize()), "runtime DXC DLL initialization without a GPU"))
        return failures;

    wchar_t modulePath[32768] = {};
    const HMODULE runtimeModule = GetModuleHandleW(L"dxcompiler.dll");
    const DWORD pathLength = runtimeModule ? GetModuleFileNameW(runtimeModule, modulePath, 32768) : 0;
    if (!expect(pathLength > 0 && pathLength < 32768, "runtime DXC loaded module path"))
        return failures;
    std::wcout << L"Runtime shader compiler DLL: " << modulePath << std::endl;
    DWORD unused = 0;
    const DWORD versionSize = GetFileVersionInfoSizeW(modulePath, &unused);
    std::vector<unsigned char> versionData(versionSize);
    VS_FIXEDFILEINFO* version = nullptr;
    UINT versionLength = 0;
    if (expect(versionSize && GetFileVersionInfoW(modulePath, 0, versionSize, versionData.data())
        && VerQueryValueW(versionData.data(), L"\\", reinterpret_cast<void**>(&version), &versionLength)
        && versionLength >= sizeof(VS_FIXEDFILEINFO) && version->dwSignature == 0xfeef04bd,
        "runtime DXC file version metadata"))
    {
        std::cout << "Runtime DXC file version: " << HIWORD(version->dwFileVersionMS) << '.'
            << LOWORD(version->dwFileVersionMS) << '.' << HIWORD(version->dwFileVersionLS) << '.'
            << LOWORD(version->dwFileVersionLS) << std::endl;
    }
#ifdef JENGINE_DXC_EXECUTABLE
    std::cout << "Separate CI shader compiler executable: " << JENGINE_DXC_EXECUTABLE << std::endl;
#endif

    wchar_t executablePath[32768] = {};
    const DWORD executableLength = GetModuleFileNameW(nullptr, executablePath, 32768);
    if (!expect(executableLength > 0 && executableLength < 32768, "runtime shader audit executable path"))
        return failures;
    try
    {
        const auto executableDirectory = std::filesystem::path(executablePath).parent_path();
        const std::filesystem::path loadedCompiler(modulePath);
        expect(std::filesystem::equivalent(loadedCompiler.parent_path(), executableDirectory),
            "runtime DXC loaded beside engine executable");
#ifdef JENGINE_DXC_EXECUTABLE
        const auto packageDirectory = std::filesystem::path(JENGINE_DXC_EXECUTABLE).parent_path();
        expect(RuntimeShaderFilesMatch(loadedCompiler, packageDirectory / "dxcompiler.dll"),
            "loaded dxcompiler DLL bytes match selected DXC package");
        // DXC 1.9 can use its internal validator. Check the deployed validator
        // without requiring or forcing its automatic load into this process.
        expect(RuntimeShaderFilesMatch(executableDirectory / "dxil.dll", packageDirectory / "dxil.dll"),
            "deployed dxil DLL bytes match selected DXC package");
#else
        std::cout << "SKIP selected DXC package byte assertions: JENGINE_DXC_EXECUTABLE is undefined in this build"
            << std::endl;
#endif
    }
    catch (const std::exception& error)
    {
        std::cerr << "Runtime DXC deployment audit failed: " << error.what() << std::endl;
        ++failures;
    }

    const EAPIType previousApi = gAPIType;
    struct RestoreApi { EAPIType Value; ~RestoreApi() { gAPIType = Value; } } restoreApi{ previousApi };
    const auto target = std::filesystem::temp_directory_path() / ("jengine-runtime-shaders-"
        + std::to_string(GetCurrentProcessId()) + "-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup
    {
        std::filesystem::path Path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(Path, error); }
    };
    try
    {
        if (!std::filesystem::create_directory(target))
            throw std::runtime_error("Cannot create unique runtime shader fixture directory");
        const Cleanup cleanup{ target };
        const auto includeDirectory = std::filesystem::current_path() / "Resource/Shaders/hlsl";
        for (const bool vulkan : { false, true })
        {
            const auto output = target / (vulkan ? "vulkan" : "dx12");
            if (ExportSurfelGIShaderFixtures(output.string().c_str(), vulkan) != EXIT_SUCCESS)
                throw std::runtime_error("Cannot export actual generated runtime shader fixtures");
            const std::vector<const wchar_t*> options = vulkan ? std::vector<const wchar_t*>{
                L"-spirv", L"-fspv-target-env=vulkan1.1spirv1.4", L"-fvk-use-scalar-layout",
                L"-fspv-extension=SPV_EXT_descriptor_indexing", L"-fspv-extension=SPV_KHR_ray_tracing",
                L"-fspv-extension=SPV_KHR_ray_query", L"-fspv-extension=SPV_EXT_shader_viewport_index_layer"
            } : std::vector<const wchar_t*>{};
            for (const bool gather : { false, true })
            {
                const char* name = gather ? "gather" : "resolve";
                const wchar_t* entry = gather ? L"SurfelGIGatherIrradianceHWRT_CS" : L"main";
                std::set<std::string> included;
                const auto source = FlattenRuntimeShader(output / (std::string(name) + ".hlsl"), includeDirectory, included);
                const auto blob = compiler.Compile(source.data(), static_cast<uint32>(source.size()),
                    L"cs_6_6", entry, false, options);
                uint32 magic = 0;
                if (blob && blob->GetBufferSize() >= sizeof(magic))
                    std::memcpy(&magic, blob->GetBufferPointer(), sizeof(magic));
                const std::string label = std::string("runtime DXC generated ") + name + (vulkan ? " SPIR-V" : " DXIL");
                if (!expect(magic == (vulkan ? 0x07230203u : 0x43425844u), label.c_str()))
                    PrintRuntimeCompilerDiagnostics(source, entry, options);
            }
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Runtime shader audit failed: " << error.what() << std::endl;
        ++failures;
    }
    return failures;
#endif
}
