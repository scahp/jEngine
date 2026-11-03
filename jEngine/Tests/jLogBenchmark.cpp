#include "pch.h"
#include "CoreDefines.h"
#include "Code/Engine/ConsoleVariables/jConsole.h"
#include "External/IMGUI/imgui.h"
#ifndef JENGINE_CONSOLE_BENCHMARK_BASELINE
#include "Core/jLog.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
    using BenchmarkClock = std::chrono::steady_clock;
    double Milliseconds(BenchmarkClock::time_point start, BenchmarkClock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    void DrawConsoleFrame(jConsole& console)
    {
        ImGui::NewFrame(); console.Render(); ImGui::Render();
    }
}

// Console comparison intentionally disables the new file sink: the old console
// had no file I/O. Both revisions use this same driver, real console/ImGui code,
// 1280x720 headless UI, Release flags, 5 warmups and 30 measured draws. Producer
// contention uses a 16ms UI cadence and 500 writes spaced by 1ms. These numbers
// measure this UI workload, not rendered game FPS or GPU/startup performance.
int RunLogBenchmark()
{
    if (ImGui::GetCurrentContext())
    {
        std::cerr << "Run the console benchmark before creating the engine UI" << std::endl;
        return EXIT_FAILURE;
    }
#ifndef JENGINE_CONSOLE_BENCHMARK_BASELINE
    jLog::Get().Shutdown();
#endif
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr; int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto& console = jConsole::Get();
    console.SetDisplayMode(EConsoleDisplayMode::Large);
    const std::string message = "Worker asset completion: mesh=scenario.fbx bytes=1048576";
    std::cout << std::fixed << std::setprecision(6);
    for (int sample = 0; sample < 3; ++sample)
    {
        for (int count : {1000, 10000, 50000})
        {
            console.ClearLog();
            for (int i = 0; i < count; ++i) console.Log(message);
#ifdef JENGINE_CONSOLE_BENCHMARK_BASELINE
            const size_t retainedBeforeContention = static_cast<size_t>(count);
            const size_t retentionCapacity = 0; // Old console has no retention bound.
#else
            const auto retention = jLog::Get().GetStats();
            const size_t retainedBeforeContention = retention.RecentCount;
            const size_t retentionCapacity = retention.RecentCapacity;
#endif
            std::vector<double> frames;
            for (int frame = 0; frame < 35; ++frame)
            {
                ImGui::NewFrame();
                const auto start = BenchmarkClock::now(); console.Render(); const auto end = BenchmarkClock::now();
                ImGui::Render();
                if (frame >= 5) frames.push_back(Milliseconds(start, end));
            }
            std::sort(frames.begin(), frames.end());
            std::atomic<bool> startWorker{false};
            std::vector<double> producerTimes; producerTimes.reserve(500);
            std::thread worker([&]
            {
                while (!startWorker.load(std::memory_order_acquire)) std::this_thread::yield();
                for (int i = 0; i < 500; ++i)
                {
                    const auto begin = BenchmarkClock::now(); console.Log(message);
                    producerTimes.push_back(Milliseconds(begin, BenchmarkClock::now()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
            startWorker.store(true, std::memory_order_release);
            const auto cadenceStart = BenchmarkClock::now();
            for (int frame = 0; frame < 40; ++frame)
            {
                DrawConsoleFrame(console);
                std::this_thread::sleep_until(cadenceStart + std::chrono::milliseconds(16 * (frame + 1)));
            }
            worker.join();
            std::sort(producerTimes.begin(), producerTimes.end());
            std::cout << "{\"benchmark\":\"console\",\"file_sink\":false,\"sample\":" << sample
                << ",\"input_records\":" << count << ",\"retained_before_contention\":" << retainedBeforeContention
                << ",\"retention_capacity\":" << retentionCapacity << ",\"render_median_ms\":" << ((frames[14] + frames[15]) * 0.5)
                << ",\"render_p95_ms\":" << frames[28] << ",\"producer_p95_ms\":" << producerTimes[474]
                << ",\"producer_p99_ms\":" << producerTimes[494] << ",\"producer_max_ms\":" << producerTimes.back()
                << ",\"render_samples_ms\":[";
            for (std::size_t i = 0; i < frames.size(); ++i) std::cout << (i ? "," : "") << frames[i];
            std::cout << "],\"producer_samples_ms\":[";
            for (std::size_t i = 0; i < producerTimes.size(); ++i) std::cout << (i ? "," : "") << producerTimes[i];
            std::cout << "]}" << std::endl;
        }
    }
    console.ClearLog(); console.SetDisplayMode(EConsoleDisplayMode::Hidden);
    ImGui::DestroyContext();

#ifndef JENGINE_CONSOLE_BENCHMARK_BASELINE
    // A separate measurement of the added file capability; there is no old
    // file sink to compare. Capacity accommodates this bounded 10000-log burst.
    const auto directory = std::filesystem::temp_directory_path()
        / ("jengine-log-benchmark-" + std::to_string(BenchmarkClock::now().time_since_epoch().count()));
    jLog logger; jLogConfig config; config.FilePath = directory / "benchmark.log"; config.QueueCapacity = 16384;
    if (!logger.Initialize(config))
    {
        std::cerr << logger.GetLastError() << std::endl;
        std::error_code error; std::filesystem::remove_all(directory, error);
        return EXIT_FAILURE;
    }
    const auto begin = BenchmarkClock::now();
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) producers.emplace_back([&]
    {
        for (int item = 0; item < 2500; ++item) logger.Write(EjLogLevel::Info, "Benchmark", message);
    });
    for (auto& producer : producers) producer.join();
    const auto submitted = BenchmarkClock::now();
    const bool stopped = logger.Shutdown(); const auto finished = BenchmarkClock::now();
    const auto stats = logger.GetStats();
    std::cout << "{\"benchmark\":\"async_file\",\"producers\":4,\"records\":10000,\"producer_wall_ms\":"
        << Milliseconds(begin, submitted) << ",\"drained_wall_ms\":" << Milliseconds(begin, finished)
        << ",\"submitted\":" << stats.Submitted << ",\"written\":" << stats.Written << ",\"dropped\":" << stats.Dropped
        << ",\"sink_errors\":" << stats.SinkErrors << "}" << std::endl;
    std::error_code error; std::filesystem::remove_all(directory, error);
    return stopped && stats.Written == 10000 && stats.Dropped == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
#else
    return EXIT_SUCCESS;
#endif
}
