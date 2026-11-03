#include "pch.h"
#include "FileLoader/jAssetImportService.h"
#include "FileLoader/jImageFileLoader.h"
#include "Scene/jMeshObject.h"
#include <fstream>
#ifdef __APPLE__
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
    template <typename Predicate>
    bool Await(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do
        {
            if (predicate())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    std::optional<jAssetImportService::Result> AwaitResult(jAssetImportService& service)
    {
        std::optional<jAssetImportService::Result> result;
        Await([&] { result = service.Poll(); return result.has_value(); });
        return result;
    }

    void WriteDDS(const std::filesystem::path& path, uint32 width = 64, uint32 height = 64)
    {
        std::array<uint32, 37> header{};
        header[0] = 0x20534444; header[1] = 124; header[2] = 0x2100F;
        header[3] = height; header[4] = width; header[7] = 1;
        header[19] = 32; header[20] = 4; header[21] = 0x30315844;
        header[27] = 0x1000; header[32] = 28; header[33] = 3; header[35] = 1;
        const std::vector<uint8> pixels(size_t(width) * height * 4, 127);
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    }

    void WriteOBJ(const std::filesystem::path& path, const char* texture = "pixel.dds")
    {
        const auto materialName = path.stem().string() + ".mtl";
        std::ofstream file(path);
        file << "mtllib " << materialName << "\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\nusemtl test\nf 1/1 2/2 3/3\n";
        std::ofstream material(path.parent_path() / materialName);
        material << "newmtl test\nKd 1 1 1\nmap_Kd " << texture << "\n";
    }
}

int RunAssetImportTests()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
        failures += !condition;
    };
    const auto directory = std::filesystem::temp_directory_path()
        / ("jengine-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    auto& loader = jModelLoader::GetInstance(); // Owner established before any worker.
    auto& images = jImageFileLoader::GetInstance();
    expect(!g_rhi && loader.IsOwnerThread(), "asset tests establish an owner without an RHI device");

    {
        const auto path = directory / "duplicate.dds";
        WriteDDS(path, 512, 512);
        const jName filename(path.string());
        const auto attempts = images.GetStats().DecodeAttempts;
        std::promise<void> start;
        const auto gate = start.get_future().share();
        std::vector<std::future<std::shared_ptr<jImageData>>> tasks;
        for (int i = 0; i < 8; ++i)
            tasks.emplace_back(std::async(std::launch::async, [&images, filename, gate] {
                gate.wait();
                return images.LoadImageDataFromFile(filename).lock();
            }));
        start.set_value();
        auto first = tasks[0].get();
        bool identical = !!first;
        for (size_t i = 1; i < tasks.size(); ++i)
            identical &= tasks[i].get() == first;
        expect(identical && images.GetStats().DecodeAttempts == attempts + 1,
            "concurrent duplicate image paths execute and publish one shared decode");
        expect(images.LoadImageDataFromFile(filename).lock() == first,
            "decoded cache retains image ownership after request futures finish");
        const auto retry = directory / "retry.dds";
        { std::ofstream file(retry); file << "invalid DDS"; }
        expect(images.LoadImageDataFromFile(jName(retry.string())).expired(), "failed image decode publishes failure");
        WriteDDS(retry);
        expect(!images.LoadImageDataFromFile(jName(retry.string())).expired(), "failed image path can decode on retry");
    }

#ifdef __APPLE__
    {
        // A FIFO deliberately blocks the real decoder's file open until we
        // connect its writer, after checking a cache hit and a distinct decode.
        const auto fifo = directory / "blocked.dds";
        const auto fastPath = directory / "independent.dds";
        WriteDDS(fastPath);
        const auto cachedPath = directory / "retry.dds";
        const bool created = ::mkfifo(fifo.c_str(), 0600) == 0;
        expect(created, "image concurrency fixture creates a bounded local FIFO");
        if (created)
        {
            std::atomic<bool> entered{false};
            auto blocked = std::async(std::launch::async, [&] {
                entered = true;
                return images.LoadImageDataFromFile(jName(fifo.string())).lock();
            });
            Await([&] { return entered.load(); });
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const auto waiting = images.GetStats().CoalescedWaits;
            auto follower = std::async(std::launch::async, [&] {
                return images.LoadImageDataFromFile(jName(fifo.string())).lock();
            });
            const bool attached = Await([&] { return images.GetStats().CoalescedWaits > waiting; });
            auto hit = std::async(std::launch::async, [&] {
                return images.LoadImageDataFromFile(jName(cachedPath.string())).lock();
            });
            auto miss = std::async(std::launch::async, [&] {
                return images.LoadImageDataFromFile(jName(fastPath.string())).lock();
            });
            bool cacheFast = hit.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
            bool decodeFast = miss.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
            int writer = -1;
            const bool opened = Await([&] { writer = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK); return writer >= 0; });
            if (writer >= 0)
                ::close(writer); // Nonseekable empty input releases DDS parsing as a failure.
            cacheFast &= !!hit.get();
            decodeFast &= !!miss.get();
            expect(opened && cacheFast, "a cached image does not wait for an unrelated blocked decode");
            expect(opened && decodeFast, "distinct image paths decode concurrently outside the cache lock");
            expect(!blocked.get(), "blocked malformed decode completes and publishes its failure");
            expect(attached && !follower.get(), "same-path waiters receive failed decode publication without hanging");
        }
    }
#endif

    {
        const auto modelPath = directory / "triangle.obj";
        const auto texturePath = directory / "pixel.dds";
        WriteDDS(texturePath);
        WriteOBJ(modelPath);
        jObject before;
        const size_t sceneObjects = jObject::GetStaticObject().size();
        auto task = std::async(std::launch::async, [&] {
            return loader.PrepareModel(modelPath.string().c_str(), directory.string().c_str());
        });
        auto prepared = task.get();
        jObject after;
        expect(prepared && prepared->GetStats().VertexCount == 3 && prepared->GetStats().IndexCount == 3
            && prepared->GetStats().SubMeshCount == 1 && prepared->GetStats().TextureCount == 1,
            "real OBJ worker preparation produces geometry streams and decoded texture without RHI");
        expect(after.GetObjectID() == before.GetObjectID() + 1 && sceneObjects == jObject::GetStaticObject().size(),
            "CPU model preparation neither allocates scene object IDs nor registers scene objects");
        std::string threadError;
        auto finalize = std::async(std::launch::async, [&loader, payload = std::move(prepared), &threadError]() mutable {
            return loader.FinalizeModel(std::move(payload), &threadError);
        });
        expect(!finalize.get() && threadError.find("owner thread") != std::string::npos,
            "worker model finalization is rejected before any RHI access");
        std::string error;
        auto cancelled = std::make_shared<std::atomic<bool>>(true);
        expect(!loader.PrepareModel(modelPath.string().c_str(), directory.string().c_str(), cancelled, &error)
            && error == "Cancelled", "pre-cancelled preparation returns no CPU payload");
        const auto retryPath = directory / "retry.obj";
        expect(!loader.PrepareModel(retryPath.string().c_str(), directory.string().c_str(), {}, &error)
            && !error.empty(), "model preparation reports an invalid file");
        WriteOBJ(retryPath);
        expect(!!loader.PrepareModel(retryPath.string().c_str(), directory.string().c_str()),
            "a failed model path may be repaired and retried");
        const auto badTexture = directory / "bad-image.dds";
        const auto badModel = directory / "bad-texture.obj";
        { std::ofstream file(badTexture); file << "corrupt referenced image"; }
        WriteOBJ(badModel, "bad-image.dds");
        expect(!loader.PrepareModel(badModel.string().c_str(), directory.string().c_str(), {}, &error)
            && error.find("bad-image.dds") != std::string::npos,
            "an existing corrupt referenced texture fails CPU preparation with its path");
        WriteDDS(badTexture);
        expect(!!loader.PrepareModel(badModel.string().c_str(), directory.string().c_str()),
            "repairing a corrupt referenced texture permits a later model retry");
    }

    {
        jThreadPool pool(2, 2);
        std::promise<void> release;
        const auto gate = release.get_future().share();
        auto blocked = pool.Submit([gate] { gate.wait(); });
        auto independent = pool.Submit([] {});
        const bool ready = independent.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
        release.set_value();
        blocked.get(); independent.get();
        expect(ready, "request-local futures do not wait for unrelated executor work");
        pool.Shutdown();
        bool rejected = false;
        try { pool.Submit([] {}); } catch (const std::runtime_error&) { rejected = true; }
        expect(rejected, "stopped executor rejects further work instead of abandoning a future");
    }

    {
        std::promise<void> release;
        const auto gate = release.get_future().share();
        std::atomic<bool> started{false};
        jAssetImportService service({2, 1}, [&](const std::string& path, const std::string& root,
            const jAssetCancellation&, std::string&) {
            if (path == "held") { started = true; gate.wait(); }
            if (root != "copied-root") throw std::runtime_error("request root was not preserved");
            return std::make_unique<jPreparedModel>();
        });
        std::string path = "held", root = "copied-root";
        const auto first = service.Request(path, root);
        path = "changed"; root = "changed";
        expect(first && Await([&] { return started.load(); }), "asynchronous requests copy path data and run on a worker");
        const auto second = service.Request("queued", "copied-root");
        const auto third = service.Request("queued2", "copied-root");
        expect(second > first && third > second && !service.Request("overflow", "copied-root")
            && service.GetStats().Queued == 2, "asset queue is bounded and accepted request IDs increase");
        expect(service.Cancel(second) && service.GetStats().Queued == 1,
            "cancelling a queued request immediately releases queue capacity");
        expect(service.Cancel(first) && !service.Cancel(first),
            "active request cancellation is explicit and idempotent");
        const auto generation = service.GetStats().Generation;
        service.CancelAll();
        const auto latest = service.Request("latest", "copied-root");
        release.set_value();
        auto result = AwaitResult(service);
        expect(result && result->Id == latest && result->Generation > generation
            && result->State == jAssetImportService::Status::Ready && !service.Poll(),
            "cancelled active work cannot publish into the latest request generation");
        service.Shutdown();
        service.Shutdown();
        expect(!service.Request("after shutdown", "copied-root") && service.GetStats().Stopping,
            "asset shutdown is idempotent and rejects subsequent requests");
    }

    {
        std::atomic<int> calls{0};
        jAssetImportService service({2, 1}, [&](const std::string& path, const std::string&,
            const jAssetCancellation&, std::string&) {
            ++calls;
            if (path == "fail") throw std::runtime_error("expected import failure");
            return std::make_unique<jPreparedModel>();
        });
        const auto failed = service.Request("fail", "");
        auto failure = AwaitResult(service);
        expect(failure && failure->Id == failed && failure->State == jAssetImportService::Status::Failed
            && failure->Error == "expected import failure", "worker exceptions publish a terminal failure result");
        const auto ready = service.Request("retry", "");
        expect(Await([&] { return service.GetStats().Ready == 1; }), "failed service remains usable for retry");
        service.Request("waiting", "");
        service.Request("waiting2", "");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        expect(calls == 2 && service.GetStats().Ready == 1 && service.GetStats().Queued == 2,
            "full completion capacity stops preparation and bounds retained CPU payloads");
        expect(service.Cancel(ready), "cancelling an unpolled completion releases its payload");
        expect(Await([&] { return service.GetStats().Ready == 1; }), "completion cancellation wakes queued preparation");
        service.Shutdown();
        const auto stopped = service.GetStats();
        expect(stopped.Stopping && !stopped.Active && !stopped.Ready && !stopped.Queued,
            "shutdown joins without deadlock when ready and request queues are full");
    }

    {
        std::atomic<bool> started{false}, finished{false};
        auto service = std::make_unique<jAssetImportService>(jAssetImportService::Limits{1, 1},
            [&](const std::string&, const std::string&, const jAssetCancellation& cancel, std::string&) {
                started = true;
                while (!cancel->load())
                    std::this_thread::yield();
                finished = true;
                return std::make_unique<jPreparedModel>();
            });
        service->Request("active", "");
        expect(Await([&] { return started.load(); }), "shutdown fixture starts an active cooperative import");
        auto wrongOwner = std::async(std::launch::async, [&] {
            return service->Request("wrong owner", "") == 0 && !service->Poll();
        });
        expect(wrongOwner.get(), "asset request and polling reject another owner thread");
        service.reset();
        expect(finished.load(), "service destruction cancels and joins active CPU preparation");
    }

    std::filesystem::remove_all(directory);
    return failures;
}
