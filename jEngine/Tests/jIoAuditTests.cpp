#include "pch.h"
#include "FileLoader/jFile.h"
#include "jCommandlineArgument.h"

namespace
{
    struct IoAuditDirectory
    {
        std::filesystem::path Path;
        ~IoAuditDirectory()
        {
            std::error_code error;
            if (!Path.empty())
                std::filesystem::remove_all(Path, error);
        }
    };
}

int RunIoAuditTests()
{
    static_assert(!std::is_copy_constructible<jFile>::value && !std::is_copy_assignable<jFile>::value,
        "An open FILE must have one owner");
    int failures = 0;
    const auto Expect = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
    };

    jCommandLineArgument arguments;
    const char* argv[] = { "jEngine", "asset directory -dx12", "-VULKAN" };
    arguments.Init(3, argv);
    Expect(arguments.GetCommandLineArguments().size() == 2 && arguments.HasArgument("-vulkan")
        && !arguments.HasArgument("-dx12"), "argv preserves spaces inside a single argument");
    arguments.Init("-DX12 -legacy");
    Expect(arguments.HasArgument("-dx12") && arguments.HasArgument("-legacy"), "legacy command line remains supported");
    arguments.Init(1, argv);
    Expect(arguments.GetCommandLineArguments().empty(), "argv initialization clears previous arguments");
    arguments.Init(3, nullptr);
    Expect(arguments.GetCommandLineArguments().empty(), "null argv is empty");

    uint32 integer = 7;
    Expect(jCommandLineArgument::TryParseUInt32("0", integer) && integer == 0
        && jCommandLineArgument::TryParseUInt32("4294967295", integer) && integer == std::numeric_limits<uint32>::max(),
        "frame count accepts exact uint32 boundaries");
    const std::string invalidIntegers[] = { "", "-1", "+1", " 1", "1 ", "12frames", "4294967296",
        "18446744073709551616", std::string("1\0suffix", 8) };
    bool rejectedIntegers = true;
    for (const auto& text : invalidIntegers)
    {
        integer = 7;
        rejectedIntegers &= !jCommandLineArgument::TryParseUInt32(text, integer) && integer == 7;
    }
    Expect(rejectedIntegers, "frame count rejects malformed, negative and overflowing values without mutation");

    float step = 7.0f;
    Expect(jCommandLineArgument::TryParseNonNegativeFloat("0", step) && step == 0.0f
        && jCommandLineArgument::TryParseNonNegativeFloat("1.25e-2", step) && step == 0.0125f,
        "fixed timestep accepts zero and finite scientific notation");
    const std::string invalidSteps[] = { "", "-0.1", "nan", "NaN", "inf", "-inf", "1e1000", "1e-1000",
        "1seconds", " 1", "1 ", std::string("1\0suffix", 8) };
    bool rejectedSteps = true;
    for (const auto& text : invalidSteps)
    {
        step = 7.0f;
        rejectedSteps &= !jCommandLineArgument::TryParseNonNegativeFloat(text, step) && step == 7.0f;
    }
    Expect(rejectedSteps, "fixed timestep rejects nonfinite, range and syntax errors without mutation");

    IoAuditDirectory temporary;
    try
    {
        const auto prefix = "jengine-io-audit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        bool created = false;
        for (int attempt = 0; attempt < 16 && !created; ++attempt)
        {
            const auto candidate = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate))
            {
                temporary.Path = candidate;
                created = true;
            }
        }
        if (!created)
            throw std::runtime_error("could not create isolated I/O fixture directory");

        const auto source = temporary.Path / "source.txt";
        const auto empty = temporary.Path / "empty.txt";
        const auto tokens = temporary.Path / "tokens.txt";
        const auto binary = temporary.Path / "binary.bin";
        const auto missing = temporary.Path / "missing.txt";
        const auto WriteFixture = [](const std::filesystem::path& filePath, const std::string& bytes)
        {
            std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!stream)
                throw std::runtime_error("could not write I/O fixture");
        };
        WriteFixture(source, "abcdef");
        WriteFixture(empty, "");
        WriteFixture(tokens, "</end><start>payload</end>tail<last>fallback");
        WriteFixture(binary, std::string("a\0b", 3));
        const auto sourceName = source.string();

        jFile file;
        Expect(file.ReadFileToBuffer() == 0 && !file.GetBuffer(), "unopened file has no readable buffer");
        Expect(file.OpenFile(sourceName.c_str()), "open isolated file");
        Expect(file.ReadFileToBuffer(true, 0, 2) == 2 && file.GetBuffer()
            && std::strcmp(file.GetBuffer(), "ab") == 0, "first partial append starts at offset zero");
        Expect(file.ReadFileToBuffer(true, 2, 2) == 2 && file.GetBuffer()
            && std::strcmp(file.GetBuffer(), "abcd") == 0, "partial append retains all previous bytes");
        Expect(file.ReadFileToBuffer(true, 4) == 2 && file.GetBuffer()
            && std::strcmp(file.GetBuffer(), "abcdef") == 0, "count zero appends only the remaining file bytes");
        Expect(file.GetBuffer(2, 3) && std::memcmp(file.GetBuffer(2, 3), "cde", 3) == 0,
            "buffer range returns its requested offset");
        Expect(file.GetBuffer(6) && *file.GetBuffer(6) == '\0'
            && !file.GetBuffer(7) && !file.GetBuffer(2, 5)
            && !file.GetBuffer(std::numeric_limits<unsigned int>::max(), std::numeric_limits<unsigned int>::max()),
            "buffer ranges enforce payload bounds without unsigned overflow");
        Expect(file.ReadFileToBuffer(false, 2) == 4 && file.GetBuffer()
            && std::strcmp(file.GetBuffer(), "cdef") == 0, "replacement count zero starts at the requested file offset");
        Expect(file.ReadFileToBuffer(false, 1, std::numeric_limits<unsigned int>::max()) == 5
            && file.GetBuffer() && std::strcmp(file.GetBuffer(), "bcdef") == 0,
            "short file caps an oversized requested read");
        Expect(file.ReadFileToBuffer(false, 100, 3) == 0 && file.GetBuffer() && *file.GetBuffer() == '\0',
            "read beyond EOF produces an empty terminated replacement");
        Expect(file.ReadFileToBuffer(false) == 6 && file.GetBuffer()
            && std::strcmp(file.GetBuffer(), "abcdef") == 0, "read recovers after EOF");
        file.CloseFile();
        Expect(file.GetBuffer() && std::strcmp(file.GetBuffer(), "abcdef") == 0,
            "closing the handle retains its owned read buffer");
        Expect(file.OpenFile(empty.string().c_str()) && file.IsBufferEmpty(), "opening another file clears previous contents");
        Expect(file.ReadFileToBuffer() == 0 && file.GetBuffer() && *file.GetBuffer() == '\0', "empty file has a terminated empty buffer");
        Expect(file.OpenFile(sourceName.c_str()) && file.ReadFileToBuffer() == 6
            && !file.OpenFile(missing.string().c_str()) && !file.GetBuffer(), "failed reopen cannot expose a stale file buffer");
        Expect(!file.OpenFile(nullptr) && !file.OpenFile("")
            && !file.OpenFile(sourceName.c_str(), FileType::BINARY, static_cast<ReadWriteType::Enum>(6)),
            "invalid file inputs fail without CRT invalid-parameter handling");

        jFile text;
        Expect(text.OpenFile(tokens.string().c_str(), FileType::TEXT) && text.ReadFileToBuffer(false) > 0,
            "open text fixture on both CRTs");
        jFile::FILE_BUFFER extracted;
        Expect(text.GetBuffer(extracted, "<start>", "</end>") && std::strcmp(extracted.data(), "payload") == 0,
            "token extraction ignores an end token before the start token");
        Expect(text.GetBuffer(extracted, "<last>", "</missing>") && std::strcmp(extracted.data(), "fallback") == 0,
            "token extraction retains the missing-end fallback");
        Expect(!text.GetBuffer(extracted, nullptr, "</end>") && !text.GetBuffer(extracted, "<start>", nullptr)
            && !text.GetBuffer(extracted, "<missing>", "</end>"), "null and absent tokens fail safely");

        jFile raw;
        Expect(raw.OpenFile(binary.string().c_str()) && raw.ReadFileToBuffer() == 3 && raw.GetBuffer(0, 3)
            && std::memcmp(raw.GetBuffer(0, 3), "a\0b\0", 4) == 0 && raw.GetBuffer(2, 1)[0] == 'b',
            "binary payload preserves embedded NULs and range offsets");
        Expect(jFile::GetFileTimeStamp(nullptr) == 0 && jFile::GetFileTimeStamp("") == 0
            && jFile::GetFileTimeStamp(missing.string().c_str()) == 0, "missing or invalid timestamps return zero");

#ifndef _WIN32
        std::filesystem::create_symlink(missing, temporary.Path / "broken.txt");
#endif
        std::vector<std::string> files;
        jFile::SearchFilesRecursive(files, temporary.Path.string(), { ".txt" });
        Expect(files.size() == 3 && std::find(files.begin(), files.end(), sourceName) != files.end(),
            "recursive file search tolerates a dangling link and filters extensions");
        files.clear();
        jFile::SearchFilesRecursive(files, missing.string(), { ".txt" });
        Expect(files.empty(), "recursive search tolerates a missing root");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL I/O fixture: " << error.what() << std::endl;
        ++failures;
    }

    std::cout << "I/O audit checks: " << (failures ? "FAILED" : "PASSED") << std::endl;
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
