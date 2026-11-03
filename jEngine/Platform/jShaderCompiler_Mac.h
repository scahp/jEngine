#pragma once
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
struct jMacShaderBlob {
    std::vector<uint8> Code;
    size_t GetBufferSize() const { return Code.size(); }
    const void* GetBufferPointer() const { return Code.data(); }
};
class jShaderCompiler_Mac {
public:
    static jShaderCompiler_Mac& Get() { static jShaderCompiler_Mac instance; return instance; }
    std::shared_ptr<jMacShaderBlob> Compile(const char* source, uint32 length, const wchar_t* model,
        const wchar_t* entry, bool rowMajor, std::vector<const wchar_t*> options) const {
        char pattern[] = "/tmp/jengine-shader-XXXXXX";
        const char* dir = mkdtemp(pattern);
        if (!dir) throw std::runtime_error("Cannot create shader temporary directory");
        struct Cleanup { std::filesystem::path Path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(Path, ec); } } cleanup{dir};
        auto input = cleanup.Path / "shader.hlsl", output = cleanup.Path / "shader.spv";
        { std::ofstream file(input); file.write(source, length); }
        auto narrow = [](const wchar_t* s) { std::string out; while (s && *s) out += char(*s++); return out; };
        std::vector<std::string> args{JENGINE_DXC_EXECUTABLE, "-T", narrow(model), "-E", narrow(entry), "-Fo", output.string()};
        for (const auto* opt : options) args.push_back(narrow(opt));
        args.push_back("-fspv-use-unknown-image-format");
        if (rowMajor) args.push_back("-Zpr");
        args.push_back(input.string());
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        pid_t child;
        int status = 0;
        if (posix_spawn(&child, argv[0], nullptr, nullptr, argv.data(), environ) != 0) {
            std::cerr << "Cannot start DXC: " << argv[0] << std::endl; return {};
        }
        while (waitpid(child, &status, 0) < 0) { if (errno != EINTR) return {}; }
        if (!WIFEXITED(status) || WEXITSTATUS(status)) return {};
        std::ifstream file(output, std::ios::binary);
        auto blob = std::make_shared<jMacShaderBlob>();
        blob->Code.assign(std::istreambuf_iterator<char>(file), {});
        return blob->Code.empty() ? nullptr : blob;
    }
};
