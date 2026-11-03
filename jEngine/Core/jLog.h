#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class EjLogLevel { Trace, Debug, Info, Warning, Error, Fatal };
inline constexpr unsigned jLogSchemaVersion = 1;

const char* jLogLevelName(EjLogLevel level);

struct jLogRecord
{
    std::uint64_t Sequence = 0;
    std::int64_t TimestampMilliseconds = 0; // UTC Unix time.
    std::uint64_t ThreadID = 0; // Native producer thread, not the sink worker.
    EjLogLevel Level = EjLogLevel::Info;
    std::string Category;
    std::string Message;
};

struct jLogConfig
{
    std::filesystem::path FilePath = "logs/jEngine.log";
    std::size_t QueueCapacity = 8192;
    std::size_t RecentCapacity = 1024;
    std::size_t MaxMessageBytes = 4096;
    std::size_t MaxCategoryBytes = 64;
    std::size_t MaxFileBytes = 4 * 1024 * 1024;
    std::size_t RetainedFiles = 3; // Current file plus archives.
};

struct jLogStats
{
    std::uint64_t Observed = 0;
    std::uint64_t Submitted = 0;
    std::uint64_t Written = 0;
    std::uint64_t Dropped = 0;
    std::uint64_t Truncated = 0;
    std::uint64_t SinkErrors = 0;
    std::size_t Queued = 0;
    std::size_t RecentCount = 0;
    std::size_t RecentCapacity = 0;
    bool FileSinkActive = false;
};

// Owns a private spdlog queue and one file worker; no global spdlog registry.
// Write/SnapshotRecent/ClearRecent/GetStats are safe on any thread. The recent
// ring also works before Initialize and after Shutdown (memory-only writes).
// Records and queued payloads own their strings; snapshots hold immutable records.
// A full queue discards the NEW file submission, increments Dropped and returns
// false. It still updates the recent ring. Normal submissions never wait for
// queue space or file I/O. Fatal additionally writes synchronously to stderr,
// including when the queue is full or allocating its record subsequently fails.
// Initialize/Flush/Shutdown serialize control operations and may wait for disk.
// Shutdown closes file admission, drains every accepted submission, flushes and
// joins the worker. Keep this object alive until all producer calls have ended.
class jLog
{
public:
    using RecordPtr = std::shared_ptr<const jLogRecord>;

    jLog();
    ~jLog();
    jLog(const jLog&) = delete;
    jLog& operator=(const jLog&) = delete;

    static jLog& Get();
    bool Initialize(const jLogConfig& config = {});
    bool Write(EjLogLevel level, std::string_view category, std::string_view message);
    // Flush includes all file submissions accepted before this call. Its control
    // message is never dropped, even when normal submissions fill the queue.
    bool Flush();
    bool Shutdown();
    jLogStats GetStats() const;
    std::string GetLastError() const;
    std::vector<RecordPtr> SnapshotRecent() const;
    void ClearRecent();

private:
    // Tests gate/fail the real worker sink to deterministically exercise a full
    // queue and flush errors; this does not substitute a different queue or sink.
    friend struct jLogTestAccess;
    bool InitializeInternal(const jLogConfig& config,
        std::function<void()> beforeWrite, std::function<void()> beforeFlush);

    struct Impl;
    std::unique_ptr<Impl> State;
};
