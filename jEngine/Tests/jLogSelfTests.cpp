#include "pch.h"
#include "Core/jLog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

struct jLogTestAccess
{
    static bool Initialize(jLog& logger, const jLogConfig& config,
        std::function<void()> beforeWrite, std::function<void()> beforeFlush = {})
    {
        return logger.InitializeInternal(config, std::move(beforeWrite), std::move(beforeFlush));
    }
};

namespace
{
    struct LogTestDirectory
    {
        std::filesystem::path Path = std::filesystem::temp_directory_path()
            / ("jengine-log-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        LogTestDirectory() { std::filesystem::create_directories(Path); }
        ~LogTestDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }
    };

    struct WorkerGate
    {
        std::mutex Mutex;
        std::condition_variable Changed;
        bool Entered = false;
        bool Released = false;

        void Wait()
        {
            std::unique_lock<std::mutex> lock(Mutex);
            Entered = true;
            Changed.notify_all();
            Changed.wait(lock, [&] { return Released; });
        }
        bool WaitUntilEntered()
        {
            std::unique_lock<std::mutex> lock(Mutex);
            return Changed.wait_for(lock, std::chrono::seconds(2), [&] { return Entered; });
        }
        void Release()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Released = true;
            Changed.notify_all();
        }
    };

    std::string ReadLogFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    std::size_t LineCount(const std::string& text)
    {
        return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    }

    std::uint64_t NumericField(const std::string& line, const std::string& name)
    {
        const auto field = line.find('"' + name + "\":");
        if (field == std::string::npos)
            return 0;
        return std::strtoull(line.c_str() + field + name.size() + 3, nullptr, 10);
    }

    jLogConfig TestConfig(const std::filesystem::path& path)
    {
        jLogConfig config;
        config.FilePath = path;
        config.QueueCapacity = 4096;
        config.RecentCapacity = 32;
        config.MaxMessageBytes = 128;
        config.MaxCategoryBytes = 16;
        config.MaxFileBytes = 1024 * 1024;
        return config;
    }
}

int RunLogSelfTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
    };
    LogTestDirectory directory;

    {
        jLog logger;
        std::string message = "startup-owned", category = "startup";
        expect(logger.Write(EjLogLevel::Warning, category, message), "log startup works without file/thread initialization");
        message.assign("changed"); category.assign("changed");
        auto held = logger.SnapshotRecent();
        expect(held.size() == 1 && held[0]->Message == "startup-owned" && held[0]->Category == "startup"
            && held[0]->Level == EjLogLevel::Warning && held[0]->TimestampMilliseconds > 0 && held[0]->ThreadID > 0,
            "log owns text and captures level/time/producer metadata");
        for (int i = 0; i < 1100; ++i)
            logger.Write(EjLogLevel::Info, "bounded", std::to_string(i));
        const auto recent = logger.SnapshotRecent();
        expect(recent.size() == 1024 && recent.front()->Message == "76" && recent.back()->Message == "1099"
            && std::is_sorted(recent.begin(), recent.end(), [](const auto& a, const auto& b) { return a->Sequence < b->Sequence; }),
            "log recent ring evicts oldest records at its fixed bound");
        logger.Write(EjLogLevel::Debug, std::string(100, 'c'), std::string(5000, 'm'));
        const auto last = logger.SnapshotRecent().back();
        expect(last->Message.size() == 4096 && last->Category.size() == 64 && logger.GetStats().Truncated == 1,
            "log bounds both category and message bytes before queueing");
        logger.ClearRecent();
        expect(logger.SnapshotRecent().empty() && held[0]->Message == "startup-owned",
            "log clear preserves lifetime of immutable snapshots");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "invalid.log");
        config.QueueCapacity = 0;
        expect(!logger.Initialize(config) && !std::filesystem::exists(config.FilePath),
            "log rejects invalid queue bounds before creating a file");
        config = TestConfig(directory.Path / "small.log"); config.MaxFileBytes = 64;
        expect(!logger.Initialize(config), "log file bound must fit a fully escaped maximum record");
        config = TestConfig(directory.Path); // Opening a directory as a file must fail on both platforms.
        expect(!logger.Initialize(config) && !logger.GetLastError().empty() && !logger.GetStats().FileSinkActive
            && logger.Write(EjLogLevel::Error, "fallback", "still visible"),
            "log file-open failure propagates and leaves memory logging available");
        config = TestConfig(directory.Path / std::filesystem::path(u8"\uCF54\uC5B4.log"));
        expect(logger.Initialize(config) && logger.GetLastError().empty(), "log can recover initialization using a Unicode file path");
        std::string text = "quote\"\\\n";
        text.push_back('\0'); text.push_back('\1'); text.push_back(static_cast<char>(0xff));
        text += " UTF-8 \xED\x95\x9C\xEA\xB8\x80";
        expect(logger.Write(EjLogLevel::Error, "asset\n\"", text) && logger.Flush(), "log explicit flush waits for the real file write");
        const auto file = ReadLogFile(config.FilePath);
        const auto record = logger.SnapshotRecent().back();
        expect(LineCount(file) == 1 && file.find("\\\"") != std::string::npos && file.find("\\\\") != std::string::npos
            && file.find("\\u000a") != std::string::npos && file.find("\\u0000") != std::string::npos
            && file.find("\\u0001") != std::string::npos && file.find("\\u00ff") != std::string::npos,
            "log JSON escapes control/NUL/quote/backslash/invalid UTF-8 bytes without line injection");
        expect(NumericField(file, "schema") == jLogSchemaVersion && NumericField(file, "sequence") == record->Sequence
            && NumericField(file, "thread_id") == record->ThreadID
            && NumericField(file, "timestamp_ms") == static_cast<std::uint64_t>(record->TimestampMilliseconds)
            && file.find("\"level\":\"error\"") != std::string::npos,
            "log file records contain versioned producer metadata");
        expect(logger.Shutdown() && logger.Shutdown(), "log shutdown is idempotent");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "utf8-truncation.log"); config.MaxMessageBytes = 2;
        expect(logger.Initialize(config) && logger.Write(EjLogLevel::Info, "UTF8", "\xED\x95\x9C") && logger.Shutdown(),
            "log truncated UTF-8 submission completes");
        const auto file = ReadLogFile(config.FilePath);
        expect(file.find("\\u00ed\\u0095") != std::string::npos
            && std::none_of(file.begin(), file.end(), [](unsigned char byte) { return byte >= 0x80; })
            && logger.GetStats().Truncated == 1,
            "log incomplete UTF-8 at the byte bound is escaped into valid JSON text");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "concurrent.log");
        expect(logger.Initialize(config), "log concurrent sink initializes");
        std::atomic<bool> stop{false}, validSnapshots{true};
        std::thread reader([&]
        {
            while (!stop.load())
            {
                const auto snapshot = logger.SnapshotRecent();
                if (snapshot.size() > config.RecentCapacity
                    || !std::is_sorted(snapshot.begin(), snapshot.end(), [](const auto& a, const auto& b) { return a->Sequence < b->Sequence; }))
                    validSnapshots.store(false);
                std::this_thread::yield();
            }
        });
        std::vector<std::thread> workers;
        for (int worker = 0; worker < 4; ++worker)
            workers.emplace_back([&, worker]
            {
                for (int item = 0; item < 400; ++item)
                    logger.Write(EjLogLevel::Debug, "Asset", "worker=" + std::to_string(worker) + " item=" + std::to_string(item));
            });
        for (auto& worker : workers) worker.join();
        stop.store(true); reader.join();
        expect(logger.Shutdown() && validSnapshots.load(), "log multiple producers and concurrent UI snapshots complete safely");
        const auto stats = logger.GetStats();
        std::istringstream input(ReadLogFile(config.FilePath));
        std::string line; std::uint64_t previous = 0; bool ordered = true;
        std::set<std::uint64_t> sequences, threads;
        while (std::getline(input, line))
        {
            const auto sequence = NumericField(line, "sequence");
            ordered &= sequence > previous; previous = sequence;
            sequences.insert(sequence); threads.insert(NumericField(line, "thread_id"));
        }
        expect(stats.Submitted == 1600 && stats.Written == 1600 && stats.Dropped == 0
            && sequences.size() == 1600 && threads.size() == 4 && ordered,
            "log accepted concurrent records drain exactly once in sequence with distinct producer threads");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "overflow.log"); config.QueueCapacity = 2;
        auto gate = std::make_shared<WorkerGate>();
        std::atomic<int> writes{0}, flushes{0};
        expect(jLogTestAccess::Initialize(logger, config, [&, gate] { if (writes.fetch_add(1) == 0) gate->Wait(); },
            [&] { ++flushes; }), "log bounded-queue real sink initializes");
        logger.Write(EjLogLevel::Info, "Queue", "accepted-first");
        expect(gate->WaitUntilEntered(), "log test gates the actual file worker");
        const bool second = logger.Write(EjLogLevel::Info, "Queue", "accepted-second");
        const bool third = logger.Write(EjLogLevel::Info, "Queue", "accepted-third");
        const bool rejected = !logger.Write(EjLogLevel::Error, "Queue", "discarded-new");
        const auto blockedStats = logger.GetStats();
        expect(second && third && rejected && blockedStats.Queued == 2 && blockedStats.Submitted == 3
            && blockedStats.Dropped == 1 && blockedStats.Written == 0 && flushes.load() == 0
            && logger.SnapshotRecent().back()->Message == "discarded-new",
            "log full queue drops new file submission without waiting/flushing and still updates UI");
        auto flushing = std::async(std::launch::async, [&] { return logger.Flush(); });
        auto producer = std::async(std::launch::async, [&] { return logger.Write(EjLogLevel::Info, "Queue", "discarded-during-flush"); });
        const bool producerReady = producer.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
        const bool producerRejected = producerReady && !producer.get();
        gate->Release();
        if (!producerReady) (void)producer.get();
        expect(producerRejected && flushing.get(), "log full-queue flush fence completes without holding the producer lock");
        const auto file = ReadLogFile(config.FilePath);
        expect(LineCount(file) == 3 && file.find("discarded") == std::string::npos && logger.GetStats().Written == 3
            && flushes.load() == 1, "log overflow preserves every previously accepted record and only explicitly flushes");
        expect(logger.Shutdown() && flushes.load() == 2, "log shutdown adds a final flush and joins the worker");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "shutdown.log"); config.QueueCapacity = 2;
        auto gate = std::make_shared<WorkerGate>(); std::atomic<int> calls{0};
        expect(jLogTestAccess::Initialize(logger, config, [&, gate] { if (calls.fetch_add(1) == 0) gate->Wait(); }),
            "log shutdown-admission sink initializes");
        logger.Write(EjLogLevel::Info, "Lifetime", "accepted-one");
        expect(gate->WaitUntilEntered(), "log shutdown test holds a real in-flight write");
        logger.Write(EjLogLevel::Info, "Lifetime", "accepted-two");
        logger.Write(EjLogLevel::Info, "Lifetime", "accepted-three");
        auto stopping = std::async(std::launch::async, [&] { return logger.Shutdown(); });
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (logger.GetStats().FileSinkActive && std::chrono::steady_clock::now() < limit) std::this_thread::yield();
        const bool admissionClosed = !logger.GetStats().FileSinkActive;
        const bool memoryAccepted = logger.Write(EjLogLevel::Info, "Lifetime", "memory-after-stop");
        const bool waiting = stopping.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready;
        gate->Release();
        expect(admissionClosed && memoryAccepted && waiting && stopping.get(),
            "log shutdown closes file admission before draining and keeps memory logging available");
        const auto file = ReadLogFile(config.FilePath);
        expect(LineCount(file) == 3 && file.find("memory-after-stop") == std::string::npos
            && logger.GetStats().Submitted == logger.GetStats().Written,
            "log shutdown drains all accepted records without admitting post-stop file writes");
        config.FilePath = directory.Path / "restart.log";
        expect(logger.Initialize(config) && logger.Write(EjLogLevel::Info, "Lifetime", "restarted") && logger.Shutdown(),
            "log owns and can restart its independent worker lifetime");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "write-error.log"); std::atomic<int> calls{0};
        expect(jLogTestAccess::Initialize(logger, config, [&]
        {
            if (calls.fetch_add(1) == 0) throw std::runtime_error("injected file write failure");
        }), "log write-error real sink initializes");
        logger.Write(EjLogLevel::Error, "Failure", "failed-record");
        logger.Write(EjLogLevel::Info, "Failure", "following-record");
        expect(!logger.Flush() && logger.GetStats().SinkErrors == 1 && logger.GetStats().Written == 1
            && logger.GetLastError().find("injected file write failure") != std::string::npos,
            "log propagates worker write errors and continues processing later records");
        expect(!logger.Shutdown(), "log shutdown reports a prior file failure after joining");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "flush-error.log");
        expect(jLogTestAccess::Initialize(logger, config, {}, [] { throw std::runtime_error("injected file flush failure"); }),
            "log flush-error real sink initializes");
        logger.Write(EjLogLevel::Info, "Failure", "before failed flush");
        expect(!logger.Flush() && logger.GetStats().SinkErrors == 1 && !logger.GetLastError().empty(),
            "log failed flush still signals its completion fence and reports the error");
        expect(!logger.Shutdown() && logger.GetStats().SinkErrors == 2,
            "log failed final flush still joins its worker without hanging");
    }

    {
        jLog logger;
        auto config = TestConfig(directory.Path / "rotation.log"); config.MaxFileBytes = 2048;
        expect(logger.Initialize(config), "log bounded rotating file sink initializes");
        for (int i = 0; i < 200; ++i) logger.Write(EjLogLevel::Info, "Rotation", std::string(96, 'x'));
        expect(logger.Shutdown(), "log rotating sink drains and closes");
        bool bounded = true, ordered = true; std::size_t files = 0, retained = 0; std::uint64_t previous = 0;
        for (int archive : {2, 1, 0})
        {
            const auto path = directory.Path / (archive ? "rotation." + std::to_string(archive) + ".log" : "rotation.log");
            if (!std::filesystem::exists(path)) { bounded = false; continue; }
            ++files; bounded &= std::filesystem::file_size(path) <= config.MaxFileBytes;
            std::istringstream input(ReadLogFile(path)); std::string line;
            while (std::getline(input, line))
            {
                const auto sequence = NumericField(line, "sequence");
                ordered &= sequence > previous; previous = sequence; ++retained;
            }
        }
        bounded &= !std::filesystem::exists(directory.Path / "rotation.3.log");
        expect(bounded && files == 3 && retained > 0 && retained < 200 && ordered && previous == 200
            && logger.GetStats().Written == 200, "log rotation bounds each file and total file count while retaining latest ordered records");
    }

    const auto destructorFile = directory.Path / "destructor.log";
    {
        jLog logger;
        expect(logger.Initialize(TestConfig(destructorFile)), "log destructor drain sink initializes");
        for (int i = 0; i < 100; ++i) logger.Write(EjLogLevel::Info, "Lifetime", "destructor drain");
    }
    expect(LineCount(ReadLogFile(destructorFile)) == 100, "log destructor drains accepted records and closes the file");
    return failures;
}
