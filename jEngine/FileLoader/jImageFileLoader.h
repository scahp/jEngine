#pragma once

#include <mutex>
#include <cstddef>
#include <condition_variable>
#include <exception>
#include <thread>

struct jImageData
{
	bool CreateMipmapIfPossible = true;
	bool sRGB = false;
	int32 Width = 0;
	int32 Height = 0;
	int32 MipLevel = 1;
	int32 LayerCount = 1;
	jName Filename;
	ETextureFormat Format = ETextureFormat::RGBA8;
	EFormatType FormatType = EFormatType::UNSIGNED_BYTE;
	ETextureType TextureType = ETextureType::TEXTURE_2D;
	jImageBulkData ImageBulkData;
};

class jImageFileLoader
{
public:
	~jImageFileLoader();

	// Apply BC3 Block compression to Windows Imaging Component (WIC), it helps gpu to increase VRAM bandwidth for reading.
	constexpr static bool UseBlockCompressionForWIC = true;

	static jImageFileLoader& GetInstance()
	{
		std::lock_guard<std::mutex> lock(GetInstanceMutex());
		if (!_instance)
			_instance = new jImageFileLoader();
		return *_instance; 
	}

	static void ReleaseInstance()
	{
		// All import/decode callers must have joined before destroying these
		// strong caches: existing materials retain raw texture pointers.
		std::lock_guard<std::mutex> lock(GetInstanceMutex());
		if (_instance)
		{
			_instance->CachedImageDataMap.clear();
			_instance->CachedTextureMap.clear();

			delete _instance;
			_instance = nullptr;
		}
	}

	std::weak_ptr<jImageData> LoadImageDataFromFile(const jName& filename);
	// GPU creation is owner-thread only; CPU preparation uses the method above.
	std::weak_ptr<jTexture> LoadTextureFromFile(const jName& filename);
	std::weak_ptr<jTexture> LoadTextureFromImageData(const std::shared_ptr<jImageData>& imageData);
    // Managed editor/model resources retain a strong material lease. Their cache
    // keys include decoded-object identity, so a reimport never returns stale pixels.
    std::shared_ptr<jTexture> FindTextureForImage(const std::shared_ptr<jImageData>& imageData);
    void StoreTextureForImage(const std::shared_ptr<jImageData>& imageData, const std::shared_ptr<jTexture>& texture);
    std::shared_ptr<jTexture> LoadTextureOwned(const jName& filename);
    // Upload an immutable CPU-prepared image with a weak identity cache. The
    // caller retains the decoded lease while a material owns the GPU lease.
    std::shared_ptr<jTexture> LoadPreparedTextureOwned(const std::shared_ptr<jImageData>& imageData);
    void InvalidateImageData(const jName& filename);
    struct Stats
    {
        size_t DecodeAttempts = 0;
        size_t CacheHits = 0;
        size_t CoalescedWaits = 0;
        size_t InFlight = 0;
        size_t Cached = 0;
    };
    Stats GetStats() const
    {
        std::lock_guard<std::mutex> lock(CachedImageDataLock);
        return {DecodeAttempts, CacheHits, CoalescedWaits, InFlightImages.size(), CachedImageDataMap.size()};
    }

private:
	std::map<jName, std::shared_ptr<jImageData> > CachedImageDataMap;
	std::map<jName, std::shared_ptr<jTexture> > CachedTextureMap;
    mutable std::mutex CachedImageDataLock;
    std::mutex CachedTextureLock;
    size_t DecodeAttempts = 0;
    size_t CacheHits = 0;
    size_t CoalescedWaits = 0;
    std::thread::id TextureOwnerThread;
    struct DecodeState
    {
        std::condition_variable Completed;
        bool Done = false;
        std::shared_ptr<jImageData> Image;
        std::exception_ptr Error;
        uint64_t Generation = 0;
    };
    std::map<jName, std::shared_ptr<DecodeState>> InFlightImages;
    struct ManagedTexture { std::weak_ptr<jImageData> Image; std::weak_ptr<jTexture> Texture; };
    std::map<const jImageData*, ManagedTexture> ManagedTextures;
    std::map<jName, uint64_t> ImageGenerations;

    std::shared_ptr<jImageData> DecodeImageData(const jName& filename);
    std::shared_ptr<jTexture> UploadImageData(const jImageData* imageData);
    std::weak_ptr<jImageData> GetOrDecodeImage(const jName& filename)
    {
        if (!filename.IsValid())
            return {};
        std::shared_ptr<DecodeState> state;
        {
            std::unique_lock<std::mutex> lock(CachedImageDataLock);
            const auto cached = CachedImageDataMap.find(filename);
            if (cached != CachedImageDataMap.end())
            {
                ++CacheHits;
                return cached->second;
            }
            const auto pending = InFlightImages.find(filename);
            if (pending != InFlightImages.end())
            {
                ++CoalescedWaits;
                state = pending->second;
                state->Completed.wait(lock, [&state] { return state->Done; });
                if (state->Error)
                    std::rethrow_exception(state->Error);
                return state->Image;
            }
            state = std::make_shared<DecodeState>();
            state->Generation = ImageGenerations[filename];
            InFlightImages.emplace(filename, state);
            ++DecodeAttempts;
        }

        std::shared_ptr<jImageData> image;
        std::exception_ptr error;
        try
        {
            image = DecodeImageData(filename); // I/O and decompression hold no cache lock.
            if (image)
            {
                std::lock_guard<std::mutex> lock(CachedImageDataLock);
                if (ImageGenerations[filename] == state->Generation)
                    CachedImageDataMap.emplace(filename, image);
            }
        }
        catch (...)
        {
            error = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(CachedImageDataLock);
            state->Image = std::move(image);
            state->Error = error;
            state->Done = true;
            const auto pending = InFlightImages.find(filename);
            if (pending != InFlightImages.end() && pending->second == state)
                InFlightImages.erase(pending); // Never erase a newer reimport flight.
        }
        state->Completed.notify_all(); // Success, decoder failure, and exception all publish.
        if (error)
            std::rethrow_exception(error);
        return state->Image;
    }

private:
	jImageFileLoader();
	static std::mutex& GetInstanceMutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	static jImageFileLoader* _instance;
};
