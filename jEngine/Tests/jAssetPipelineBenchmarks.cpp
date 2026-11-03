#include "pch.h"
#include "FileLoader/jImageFileLoader.h"
#include <fstream>
#include <iomanip>

// CPU image-cache/decode API only: no device, GPU upload or scene mutation.
// Run in a fresh benchmark process; all workers are joined before cache reset.
int RunAssetDecodeBenchmarks(const char* fixtureDirectory, const char* outputPath)
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(fixtureDirectory))
        if (entry.is_regular_file() && entry.path().extension() == ".png")
            files.push_back(std::filesystem::absolute(entry.path()));
    std::sort(files.begin(), files.end());
    if (files.empty()) return EXIT_FAILURE;
    std::ofstream output(outputPath);
    if (!output) return EXIT_FAILURE;
    output << std::fixed << std::setprecision(3)
        << "{\n  \"benchmark\": \"production-image-decode\",\n"
        << "  \"conditions\": \"Same PNG files; warm OS filesystem cache; cold engine cache each trial; no GPU; thread start gate excludes thread creation.\",\n"
        << "  \"files\": " << files.size() << ",\n  \"samples\": [\n";
    bool first = true;
    int failures = 0;
    for (const int workerCount : {1, 8})
    {
        for (int trial = 0; trial < 5; ++trial)
        {
            jImageFileLoader::ReleaseInstance();
            auto& loader = jImageFileLoader::GetInstance();
            std::atomic<size_t> next{0};
            std::atomic<int> invalid{0};
            std::mutex gateMutex;
            std::condition_variable gate;
            bool go = false;
            std::vector<std::thread> workers;
            for (int worker = 0; worker < workerCount; ++worker)
            {
                workers.emplace_back([&] {
#ifdef _WIN32
                    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
                    {
                        std::unique_lock<std::mutex> lock(gateMutex);
                        gate.wait(lock, [&] { return go; });
                    }
                    for (size_t index = next.fetch_add(1); index < files.size(); index = next.fetch_add(1))
                    {
                        const auto path = files[index].string();
                        auto data = loader.LoadImageDataFromFile(jName(path.c_str())).lock();
                        if (!data || data->Width != 1024 || data->Height != 1024 || data->ImageBulkData.ImageData.empty())
                            ++invalid;
                    }
#ifdef _WIN32
                    if (SUCCEEDED(com)) CoUninitialize();
#endif
                });
            }
            const auto start = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                go = true;
            }
            gate.notify_all();
            for (auto& worker : workers) worker.join();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            failures += invalid.load();
            if (!first) output << ",\n";
            first = false;
            output << "    {\"workers\": " << workerCount << ", \"trial\": " << trial
                << ", \"elapsed_ms\": " << ms << ", \"invalid\": " << invalid << "}";
            std::cout << "Image decode " << workerCount << " workers trial " << trial << ": " << ms << " ms" << std::endl;
        }
    }
    jImageFileLoader::ReleaseInstance();
    output << "\n  ]\n}\n";
    output.flush();
    return output.good() && !failures ? EXIT_SUCCESS : EXIT_FAILURE;
}
