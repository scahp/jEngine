#pragma once

#include "jEditorAssetPath.h"
#include "assimp/IOSystem.hpp"
#include "assimp/IOStream.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>

// Read-only adapter for Assimp's UTF-8 filenames, including external MTL/buffer
// files. The importer owns the handler and each stream owns its native file.
class jAssimpUtf8IO final : public Assimp::IOSystem
{
    class Stream final : public Assimp::IOStream
    {
        std::FILE* File = nullptr;
        size_t Size = 0;
        static std::int64_t Position(std::FILE* file)
        {
#ifdef _WIN32
            return _ftelli64(file);
#else
            return ftello(file);
#endif
        }
        static int SetPosition(std::FILE* file, std::int64_t offset, int origin)
        {
#ifdef _WIN32
            return _fseeki64(file, offset, origin);
#else
            return fseeko(file, static_cast<off_t>(offset), origin);
#endif
        }
    public:
        explicit Stream(std::FILE* file) : File(file)
        {
            if (!SetPosition(File, 0, SEEK_END))
            {
                const auto size = Position(File);
                if (size > 0 && static_cast<std::uint64_t>(size) <= std::numeric_limits<size_t>::max()) Size = static_cast<size_t>(size);
            }
            SetPosition(File, 0, SEEK_SET);
        }
        ~Stream() override { std::fclose(File); }
        size_t Read(void* destination, size_t size, size_t count) override { return std::fread(destination, size, count, File); }
        size_t Write(const void*, size_t, size_t) override { return 0; }
        aiReturn Seek(size_t offset, aiOrigin origin) override
        {
            const int nativeOrigin = origin == aiOrigin_SET ? SEEK_SET : origin == aiOrigin_CUR ? SEEK_CUR : SEEK_END;
            return SetPosition(File, static_cast<std::int64_t>(offset), nativeOrigin) ? aiReturn_FAILURE : aiReturn_SUCCESS;
        }
        size_t Tell() const override { const auto offset = Position(File); return offset < 0 ? 0 : static_cast<size_t>(offset); }
        size_t FileSize() const override { return Size; }
        void Flush() override {}
    };
    static std::filesystem::path Native(const char* filename)
    {
        std::string normalized = filename ? filename : "";
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return jEditorAssetPath::FromUtf8(normalized);
    }
public:
    bool Exists(const char* filename) const override
    {
        try { std::error_code error; return std::filesystem::is_regular_file(Native(filename), error); }
        catch (...) { return false; }
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* filename, const char* mode = "rb") override
    {
        if (!filename || !mode || std::strpbrk(mode, "wa+")) return nullptr;
        try
        {
            const auto path = Native(filename);
#ifdef _WIN32
            std::FILE* file = nullptr;
            _wfopen_s(&file, path.c_str(), L"rb");
#else
            auto* file = std::fopen(path.c_str(), "rb");
#endif
            if (!file) return nullptr;
            try { return new Stream(file); }
            catch (...) { std::fclose(file); throw; }
        }
        catch (...) { return nullptr; }
    }
    void Close(Assimp::IOStream* stream) override { delete stream; }
    bool ComparePaths(const char* first, const char* second) const override
    {
        if (!first || !second) return false;
        try
        {
            std::error_code error;
            if (std::filesystem::equivalent(Native(first), Native(second), error)) return true;
            return Native(first).lexically_normal() == Native(second).lexically_normal();
        }
        catch (...) { return false; }
    }
};
