#include "pch.h"
#include "jImageFileLoader.h"

std::shared_ptr<jTexture> jImageFileLoader::FindTextureForImage(const std::shared_ptr<jImageData>& image)
{
    if (!image) return {};
    std::lock_guard<std::mutex> lock(CachedTextureLock);
    const auto found = ManagedTextures.find(image.get());
    if (found == ManagedTextures.end()) return {};
    if (found->second.Image.lock() != image) { ManagedTextures.erase(found); return {}; }
    auto texture = found->second.Texture.lock();
    if (!texture) ManagedTextures.erase(found);
    return texture;
}
void jImageFileLoader::StoreTextureForImage(const std::shared_ptr<jImageData>& image, const std::shared_ptr<jTexture>& texture)
{
    if (!image || !texture) return;
    std::lock_guard<std::mutex> lock(CachedTextureLock);
    // No strong cache retention: the material/prototype owns the live GPU lease.
    for (auto it = ManagedTextures.begin(); it != ManagedTextures.end(); )
        if (it->second.Image.expired() || it->second.Texture.expired()) it = ManagedTextures.erase(it); else ++it;
    ManagedTextures[image.get()] = {image, texture};
}
std::shared_ptr<jTexture> jImageFileLoader::LoadTextureOwned(const jName& filename)
{
    auto image = LoadImageDataFromFile(filename).lock();
    if (!image) return {};
    if (auto cached = FindTextureForImage(image)) return cached;
    if (!g_rhi || (GMainThreadID != std::thread::id() && GMainThreadID != std::this_thread::get_id())) return {};
    auto texture = g_rhi->CreateTextureFromData(image.get());
    StoreTextureForImage(image, texture);
    return texture;
}
void jImageFileLoader::InvalidateImageData(const jName& filename)
{
    std::lock_guard<std::mutex> lock(CachedImageDataLock);
    ++ImageGenerations[filename];
    CachedImageDataMap.erase(filename);
    // Existing callers may finish their immutable old generation. New callers
    // start a separate flight; the completion guard cannot erase that flight.
    InFlightImages.erase(filename);
}
