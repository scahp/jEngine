#include "pch.h"
#include "jLog.h"

#ifdef _WIN32
#define SPDLOG_WCHAR_FILENAMES
#endif
#include <spdlog/async_logger.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <utility>

namespace
{
    struct LogDiagnostics
    {
        std::atomic<std::uint64_t> Written{0};
        std::atomic<std::uint64_t> Errors{0};
        std::atomic<bool> Failed{false};
        mutable std::mutex Mutex;
        std::string LastError;

        void Fail(std::string_view message) noexcept
        {
            ++Errors;
            Failed.store(true);
            try
            {
                std::lock_guard<std::mutex> lock(Mutex);
                LastError.assign(message.substr(0, 1024));
            }
            catch (...) { } // An allocation failure must not kill the log worker.
        }
    };

    struct LogFlushFence
    {
        std::mutex Mutex;
        std::condition_variable Complete;
        std::uint64_t Count = 0;
    };

    // Only the spdlog worker calls log/flush. Completion is signalled even when
    // the real file sink fails, so explicit flush/shutdown cannot lose its fence.
    class ObservedFileSink final : public spdlog::sinks::sink
    {
    public:
        ObservedFileSink(std::shared_ptr<spdlog::sinks::sink> file,
            std::shared_ptr<LogDiagnostics> diagnostics, std::shared_ptr<LogFlushFence> fence,
            std::function<void()> beforeWrite, std::function<void()> beforeFlush)
            : File(std::move(file)), Diagnostics(std::move(diagnostics)), Fence(std::move(fence)),
              BeforeWrite(std::move(beforeWrite)), BeforeFlush(std::move(beforeFlush)) { }

        void log(const spdlog::details::log_msg& message) override
        {
            try
            {
                if (BeforeWrite)
                    BeforeWrite();
                File->log(message);
                ++Diagnostics->Written;
            }
            catch (const std::exception& error) { Diagnostics->Fail(error.what()); }
            catch (...) { Diagnostics->Fail("Unknown log file write failure"); }
        }

        void flush() override
        {
            try
            {
                if (BeforeFlush)
                    BeforeFlush();
                File->flush();
            }
            catch (const std::exception& error) { Diagnostics->Fail(error.what()); }
            catch (...) { Diagnostics->Fail("Unknown log file flush failure"); }
            {
                std::lock_guard<std::mutex> lock(Fence->Mutex);
                ++Fence->Count;
            }
            Fence->Complete.notify_all();
        }

        void set_pattern(const std::string& pattern) override { File->set_pattern(pattern); }
        void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override
        {
            File->set_formatter(std::move(formatter));
        }

    private:
        std::shared_ptr<spdlog::sinks::sink> File;
        std::shared_ptr<LogDiagnostics> Diagnostics;
        std::shared_ptr<LogFlushFence> Fence;
        std::function<void()> BeforeWrite;
        std::function<void()> BeforeFlush;
    };

    struct LogSession
    {
        std::shared_ptr<spdlog::details::thread_pool> Pool;
        std::shared_ptr<spdlog::async_logger> Backend;
        std::shared_ptr<LogFlushFence> Fence;
    };

    void DrainAndFlush(const std::shared_ptr<LogSession>& session)
    {
        std::uint64_t target = 0;
        {
            std::lock_guard<std::mutex> lock(session->Fence->Mutex);
            target = session->Fence->Count + 1;
        }
        // async_logger::flush uses the discard policy too. Post a blocking
        // control message explicitly, outside every producer/recent-ring lock.
        session->Pool->post_flush(std::shared_ptr<spdlog::async_logger>(session->Backend),
            spdlog::async_overflow_policy::block);
        std::unique_lock<std::mutex> lock(session->Fence->Mutex);
        session->Fence->Complete.wait(lock, [&] { return session->Fence->Count >= target; });
    }

    bool ValidConfig(const jLogConfig& config)
    {
        // These upper bounds also keep record-size arithmetic below SIZE_MAX.
        return !config.FilePath.empty()
            && config.QueueCapacity >= 1 && config.QueueCapacity <= 65536
            && config.RecentCapacity >= 1 && config.RecentCapacity <= 16384
            && config.MaxMessageBytes >= 1 && config.MaxMessageBytes <= 65536
            && config.MaxCategoryBytes >= 1 && config.MaxCategoryBytes <= 256
            && config.RetainedFiles >= 1 && config.RetainedFiles <= 32
            && config.MaxFileBytes >= 256 + 6 * (config.MaxMessageBytes + config.MaxCategoryBytes)
            && config.MaxFileBytes <= 1024 * 1024 * 1024;
    }

    // Encode control bytes, embedded NULs and invalid/truncated UTF-8. Every
    // record remains one valid JSON line; user text cannot inject new records.
    void AppendJsonString(std::string& output, std::string_view value)
    {
        constexpr char Hex[] = "0123456789abcdef";
        output += '"';
        for (std::size_t i = 0; i < value.size();)
        {
            const auto byte = static_cast<unsigned char>(value[i]);
            if (byte == '"' || byte == '\\')
            {
                output += '\\';
                output += static_cast<char>(byte);
                ++i;
            }
            else if (byte >= 0x20 && byte < 0x80)
            {
                output += static_cast<char>(byte);
                ++i;
            }
            else
            {
                std::size_t length = 0;
                if (byte >= 0xc2 && byte <= 0xdf) length = 2;
                else if (byte >= 0xe0 && byte <= 0xef) length = 3;
                else if (byte >= 0xf0 && byte <= 0xf4) length = 4;
                bool valid = length && i + length <= value.size();
                for (std::size_t j = 1; valid && j < length; ++j)
                    valid = (static_cast<unsigned char>(value[i + j]) & 0xc0) == 0x80;
                if (valid)
                {
                    const auto second = static_cast<unsigned char>(value[i + 1]);
                    valid = !(byte == 0xe0 && second < 0xa0) && !(byte == 0xed && second >= 0xa0)
                        && !(byte == 0xf0 && second < 0x90) && !(byte == 0xf4 && second >= 0x90);
                }
                if (valid)
                {
                    output.append(value.data() + i, length);
                    i += length;
                }
                else
                {
                    output += "\\u00";
                    output += Hex[byte >> 4];
                    output += Hex[byte & 15];
                    ++i;
                }
            }
        }
        output += '"';
    }

    std::string EncodeRecord(const jLogRecord& record)
    {
        std::string output;
        output.reserve(record.Message.size() + record.Category.size() + 192);
        output += "{\"schema\":" + std::to_string(jLogSchemaVersion);
        output += ",\"sequence\":" + std::to_string(record.Sequence);
        output += ",\"timestamp_ms\":" + std::to_string(record.TimestampMilliseconds);
        output += ",\"thread_id\":" + std::to_string(record.ThreadID);
        output += ",\"level\":\"";
        output += jLogLevelName(record.Level);
        output += "\",\"category\":";
        AppendJsonString(output, record.Category);
        output += ",\"message\":";
        AppendJsonString(output, record.Message);
        output += '}';
        return output;
    }
}

struct jLog::Impl
{
    mutable std::mutex Mutex;
    std::mutex ControlMutex;
    std::vector<RecordPtr> Recent = std::vector<RecordPtr>(1024);
    std::size_t Head = 0;
    std::size_t Count = 0;
    std::size_t MaxMessageBytes = 4096;
    std::size_t MaxCategoryBytes = 64;
    std::uint64_t NextSequence = 1;
    jLogStats Counters;
    std::shared_ptr<LogSession> Session;
    std::shared_ptr<LogDiagnostics> Diagnostics = std::make_shared<LogDiagnostics>();
};

const char* jLogLevelName(EjLogLevel level)
{
    switch (level)
    {
    case EjLogLevel::Trace: return "trace";
    case EjLogLevel::Debug: return "debug";
    case EjLogLevel::Info: return "info";
    case EjLogLevel::Warning: return "warning";
    case EjLogLevel::Error: return "error";
    case EjLogLevel::Fatal: return "fatal";
    }
    return "unknown";
}

jLog::jLog() : State(std::make_unique<Impl>()) { }
jLog::~jLog() { Shutdown(); }

jLog& jLog::Get()
{
    static jLog instance;
    return instance;
}

bool jLog::Initialize(const jLogConfig& config)
{
    return InitializeInternal(config, {}, {});
}

bool jLog::InitializeInternal(const jLogConfig& config,
    std::function<void()> beforeWrite, std::function<void()> beforeFlush)
{
    std::lock_guard<std::mutex> control(State->ControlMutex);
    if (!ValidConfig(config))
    {
        State->Diagnostics->Fail("Invalid log limits/path; file size must fit a fully escaped record");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(State->Mutex);
        if (State->Session)
        {
            State->Diagnostics->Fail("Log file sink already initialized");
            return false;
        }
    }
    try
    {
        // Directory creation/opening is control-thread work outside the producer
        // lock. Every write, rotation and explicit flush then runs on the worker.
        const auto parent = config.FilePath.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
        auto session = std::make_shared<LogSession>();
        session->Fence = std::make_shared<LogFlushFence>();
        auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            config.FilePath.native(), config.MaxFileBytes, config.RetainedFiles - 1, true);
        file->set_pattern("%v");
        auto observed = std::make_shared<ObservedFileSink>(file, State->Diagnostics, session->Fence,
            std::move(beforeWrite), std::move(beforeFlush));
        session->Pool = std::make_shared<spdlog::details::thread_pool>(config.QueueCapacity, 1);
        session->Backend = std::make_shared<spdlog::async_logger>("jEngine", observed, session->Pool,
            spdlog::async_overflow_policy::discard_new);
        session->Backend->set_level(spdlog::level::trace);
        session->Backend->set_error_handler([diagnostics = State->Diagnostics](const std::string& error)
        {
            diagnostics->Fail(error);
        });
        // Allocate the ring before the lock and retain as many startup messages
        // as the new bound permits. No startup message is replayed to disk.
        std::vector<RecordPtr> recent(config.RecentCapacity);
        {
            std::lock_guard<std::mutex> lock(State->Diagnostics->Mutex);
            State->Diagnostics->LastError.clear();
        }
        State->Diagnostics->Failed.store(false);
        {
            std::lock_guard<std::mutex> lock(State->Mutex);
            const auto keep = (std::min)(State->Count, recent.size());
            for (std::size_t i = 0; i < keep; ++i)
                recent[i] = State->Recent[(State->Head + State->Count - keep + i) % State->Recent.size()];
            State->Recent.swap(recent);
            State->Head = 0;
            State->Count = keep;
            State->MaxMessageBytes = config.MaxMessageBytes;
            State->MaxCategoryBytes = config.MaxCategoryBytes;
            State->Session = std::move(session);
        }
        return true;
    }
    catch (const std::exception& error) { State->Diagnostics->Fail(error.what()); }
    catch (...) { State->Diagnostics->Fail("Unknown log initialization failure"); }
    return false;
}

bool jLog::Write(EjLogLevel level, std::string_view category, std::string_view message)
{
    if (level == EjLogLevel::Fatal)
    {
        std::size_t length = 0;
        {
            std::lock_guard<std::mutex> lock(State->Mutex);
            length = (std::min)(message.size(), State->MaxMessageBytes);
        }
        // The emergency path needs no string allocation and no queue capacity.
        // Never perform even this stderr I/O while holding the producer lock.
        std::fputs("[FATAL] ", stderr);
        std::fwrite(message.data(), 1, length, stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }
    try
    {
        auto record = std::make_shared<jLogRecord>();
        record->TimestampMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        record->ThreadID = spdlog::details::os::thread_id();
        record->Level = level;
        bool accepted = true;
        {
            std::lock_guard<std::mutex> lock(State->Mutex);
            record->Category.assign(category.substr(0, State->MaxCategoryBytes));
            record->Message.assign(message.substr(0, State->MaxMessageBytes));
            record->Sequence = State->NextSequence++;
            ++State->Counters.Observed;
            State->Counters.Truncated += category.size() > State->MaxCategoryBytes
                || message.size() > State->MaxMessageBytes;
            if (State->Count < State->Recent.size())
                State->Recent[(State->Head + State->Count++) % State->Recent.size()] = record;
            else
            {
                State->Recent[State->Head] = record;
                State->Head = (State->Head + 1) % State->Recent.size();
            }
            if (State->Session)
            {
                const auto discardedBefore = State->Session->Pool->discard_counter();
                const auto payload = EncodeRecord(*record);
                const auto levelValue = level == EjLogLevel::Warning ? spdlog::level::warn
                    : level == EjLogLevel::Error ? spdlog::level::err
                    : level == EjLogLevel::Fatal ? spdlog::level::critical
                    : level == EjLogLevel::Debug ? spdlog::level::debug
                    : level == EjLogLevel::Trace ? spdlog::level::trace : spdlog::level::info;
                State->Session->Backend->log(levelValue, spdlog::string_view_t(payload.data(), payload.size()));
                const auto dropped = State->Session->Pool->discard_counter() - discardedBefore;
                State->Counters.Dropped += dropped;
                State->Counters.Submitted += !dropped;
                accepted = !dropped;
            }
        }
        return accepted;
    }
    catch (const std::exception& error) { State->Diagnostics->Fail(error.what()); }
    catch (...) { State->Diagnostics->Fail("Unknown log submission failure"); }
    return false;
}

bool jLog::Flush()
{
    std::lock_guard<std::mutex> control(State->ControlMutex);
    std::shared_ptr<LogSession> session;
    {
        std::lock_guard<std::mutex> lock(State->Mutex);
        session = State->Session;
    }
    if (session)
        DrainAndFlush(session);
    return !State->Diagnostics->Failed.load();
}

bool jLog::Shutdown()
{
    std::lock_guard<std::mutex> control(State->ControlMutex);
    std::shared_ptr<LogSession> session;
    {
        std::lock_guard<std::mutex> lock(State->Mutex);
        session = std::move(State->Session);
    }
    if (session)
    {
        DrainAndFlush(session);
        session->Pool.reset(); // spdlog posts terminate after the drain, then joins.
        session->Backend.reset(); // Close the file on this control path.
    }
    return !State->Diagnostics->Failed.load();
}

jLogStats jLog::GetStats() const
{
    std::lock_guard<std::mutex> lock(State->Mutex);
    auto stats = State->Counters;
    stats.Written = State->Diagnostics->Written.load();
    stats.SinkErrors = State->Diagnostics->Errors.load();
    stats.RecentCount = State->Count;
    stats.RecentCapacity = State->Recent.size();
    stats.FileSinkActive = State->Session != nullptr;
    stats.Queued = State->Session ? State->Session->Pool->queue_size() : 0;
    return stats;
}

std::string jLog::GetLastError() const
{
    std::lock_guard<std::mutex> lock(State->Diagnostics->Mutex);
    return State->Diagnostics->LastError;
}

std::vector<jLog::RecordPtr> jLog::SnapshotRecent() const
{
    std::lock_guard<std::mutex> lock(State->Mutex);
    std::vector<RecordPtr> result;
    result.reserve(State->Count);
    for (std::size_t i = 0; i < State->Count; ++i)
        result.push_back(State->Recent[(State->Head + i) % State->Recent.size()]);
    return result;
}

void jLog::ClearRecent()
{
    std::lock_guard<std::mutex> lock(State->Mutex);
    for (auto& record : State->Recent)
        record.reset();
    State->Head = State->Count = 0;
}
