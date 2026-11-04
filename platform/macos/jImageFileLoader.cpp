#include "pch.h"
#include "FileLoader/jImageFileLoader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define TINYDDSLOADER_IMPLEMENTATION
#include "tinyddsloader/tinyddsloader.h"
jImageFileLoader* jImageFileLoader::_instance = nullptr;
jImageFileLoader::jImageFileLoader() = default;
jImageFileLoader::~jImageFileLoader() = default;
std::weak_ptr<jImageData> jImageFileLoader::LoadImageDataFromFile(const jName& filename)
{
    return GetOrDecodeImage(filename);
}

std::shared_ptr<jImageData> jImageFileLoader::DecodeImageData(const jName& filename)
{
    auto data = std::make_shared<jImageData>();
    data->Filename = filename;
    auto extension = std::filesystem::path(filename.ToStr()).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return std::tolower(value); });
    if (extension == ".dds") {
        tinyddsloader::DDSFile dds;
        if (dds.Load(filename.ToStr()) != tinyddsloader::Success) {
            std::cerr << "DDS load failed: " << filename.ToStr() << std::endl;
            return {};
        }
        // Our upload path handles 2D textures, 2D arrays and a single cube.
        if (dds.GetTextureDimension() != tinyddsloader::DDSFile::TextureDimension::Texture2D
            || !dds.GetWidth() || !dds.GetHeight() || !dds.GetMipCount() || !dds.GetArraySize()
            || dds.GetWidth() > INT32_MAX || dds.GetHeight() > INT32_MAX
            || (dds.IsCubemap() && dds.GetArraySize() != 6)) {
            std::cerr << "Unsupported DDS dimensions: " << filename.ToStr() << std::endl;
            return {};
        }
        using F = tinyddsloader::DDSFile::DXGIFormat;
        switch (dds.GetFormat()) {
        case F::R16G16B16A16_Float: data->Format = ETextureFormat::RGBA16F; break;
        case F::R32G32B32A32_Float: data->Format = ETextureFormat::RGBA32F; break;
        case F::R8G8B8A8_UNorm: data->Format = ETextureFormat::RGBA8; break;
        case F::R8G8B8A8_UNorm_SRGB: data->Format = ETextureFormat::RGBA8; data->sRGB = true; break;
        default:
            std::cerr << "Unsupported DDS format: " << static_cast<int>(dds.GetFormat()) << ": " << filename.ToStr() << std::endl;
            return {};
        }
        data->FormatType = GetTexturePixelType(data->Format);
        data->Width = dds.GetWidth(); data->Height = dds.GetHeight();
        data->MipLevel = dds.GetMipCount(); data->LayerCount = dds.GetArraySize();
        data->CreateMipmapIfPossible = false;
        data->TextureType = dds.IsCubemap() ? ETextureType::TEXTURE_CUBE
            : (dds.GetArraySize() > 1 ? ETextureType::TEXTURE_2D_ARRAY : ETextureType::TEXTURE_2D);
        for (uint32 layer = 0; layer < dds.GetArraySize(); ++layer) {
            for (uint32 mip = 0; mip < dds.GetMipCount(); ++mip) {
                const auto* image = dds.GetImageData(mip, layer);
                if (!image || !image->m_mem || !image->m_memSlicePitch)
                    return {};
                jImageSubResourceData footprint{};
                footprint.Width = image->m_width; footprint.Height = image->m_height;
                footprint.RowPitch = image->m_memPitch; footprint.Offset = data->ImageBulkData.ImageData.size();
                footprint.Depth = layer; footprint.MipLevel = mip;
                const auto* begin = static_cast<const uint8*>(image->m_mem);
                data->ImageBulkData.ImageData.insert(data->ImageBulkData.ImageData.end(), begin, begin + image->m_memSlicePitch);
                data->ImageBulkData.SubresourceFootprints.push_back(footprint);
            }
        }
        return data;
    }
    int channels = 0;
    const bool hdr = stbi_is_hdr(filename.ToStr());
    std::unique_ptr<void, decltype(&stbi_image_free)> pixels(
        hdr ? (void*)stbi_loadf(filename.ToStr(), &data->Width, &data->Height, &channels, 4)
            : (void*)stbi_load(filename.ToStr(), &data->Width, &data->Height, &channels, 4), stbi_image_free);
    if (!pixels) { std::cerr << "Image load failed: " << filename.ToStr() << ": " << stbi_failure_reason() << std::endl; return {}; }
    data->Format = hdr ? ETextureFormat::RGBA32F : ETextureFormat::RGBA8;
    data->FormatType = hdr ? EFormatType::FLOAT : EFormatType::UNSIGNED_BYTE;
    const size_t pitch = static_cast<size_t>(data->Width) * 4 * (hdr ? sizeof(float) : 1);
    data->ImageBulkData.ImageData.resize(pitch * data->Height);
    std::memcpy(data->ImageBulkData.ImageData.data(), pixels.get(), data->ImageBulkData.ImageData.size());
    data->ImageBulkData.SubresourceFootprints.resize(1);
    auto& footprint = data->ImageBulkData.SubresourceFootprints[0];
    footprint.Width = data->Width; footprint.Height = data->Height;
    footprint.RowPitch = pitch; footprint.Offset = 0; footprint.Depth = 0; footprint.MipLevel = 0;
    return data;
}
std::weak_ptr<jTexture> jImageFileLoader::LoadTextureFromFile(const jName& filename)
{
    {
        std::lock_guard<std::mutex> lock(CachedTextureLock);
        const auto cached = CachedTextureMap.find(filename);
        if (cached != CachedTextureMap.end())
            return cached->second;
    }
    return LoadTextureFromImageData(LoadImageDataFromFile(filename).lock());
}

std::weak_ptr<jTexture> jImageFileLoader::LoadTextureFromImageData(const std::shared_ptr<jImageData>& imageData)
{
    if (!imageData)
        return {};
    {
        std::lock_guard<std::mutex> lock(CachedTextureLock);
        const auto cached = CachedTextureMap.find(imageData->Filename);
        if (cached != CachedTextureMap.end())
            return cached->second;
        const auto current = std::this_thread::get_id();
        if (GMainThreadID != std::thread::id() && current != GMainThreadID)
            return {};
        if (TextureOwnerThread == std::thread::id())
            TextureOwnerThread = current;
        if (TextureOwnerThread != current)
            return {};
    }
    auto texture = UploadImageData(imageData.get());
    if (texture)
    {
        std::lock_guard<std::mutex> lock(CachedTextureLock);
        CachedTextureMap.emplace(imageData->Filename, texture);
    }
    return texture;
}

std::shared_ptr<jTexture> jImageFileLoader::UploadImageData(const jImageData* imageData)
{
    return g_rhi ? g_rhi->CreateTextureFromData(imageData) : nullptr;
}
