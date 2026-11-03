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
    jScopedLock lock(&CachedImageDataLock);
    auto it = CachedImageDataMap.find(filename);
    if (it != CachedImageDataMap.end()) return it->second;
    auto data = std::make_shared<jImageData>();
    data->Filename = filename;
    if (std::filesystem::path(filename.ToStr()).extension() == ".dds") {
        tinyddsloader::DDSFile dds;
        if (dds.Load(filename.ToStr()) != tinyddsloader::Success) throw std::runtime_error("DDS load failed: " + std::string(filename.ToStr()));
        using F = tinyddsloader::DDSFile::DXGIFormat;
        switch (dds.GetFormat()) {
        case F::R16G16B16A16_Float: data->Format = ETextureFormat::RGBA16F; break;
        case F::R32G32B32A32_Float: data->Format = ETextureFormat::RGBA32F; break;
        case F::R8G8B8A8_UNorm: data->Format = ETextureFormat::RGBA8; break;
        default: throw std::runtime_error("Unsupported DDS format: " + std::to_string((int)dds.GetFormat()));
        }
        data->FormatType = GetTexturePixelType(data->Format);
        data->Width = dds.GetWidth(); data->Height = dds.GetHeight();
        data->MipLevel = dds.GetMipCount(); data->LayerCount = dds.GetArraySize();
        data->CreateMipmapIfPossible = false;
        data->TextureType = dds.IsCubemap() ? ETextureType::TEXTURE_CUBE : ETextureType::TEXTURE_2D;
        for (uint32 layer = 0; layer < dds.GetArraySize(); ++layer) {
            for (uint32 mip = 0; mip < dds.GetMipCount(); ++mip) {
                const auto* image = dds.GetImageData(mip, layer);
                jImageSubResourceData footprint{};
                footprint.Width = image->m_width; footprint.Height = image->m_height;
                footprint.RowPitch = image->m_memPitch; footprint.Offset = data->ImageBulkData.ImageData.size();
                footprint.Depth = layer; footprint.MipLevel = mip;
                const auto* begin = static_cast<const uint8*>(image->m_mem);
                data->ImageBulkData.ImageData.insert(data->ImageBulkData.ImageData.end(), begin, begin + image->m_memSlicePitch);
                data->ImageBulkData.SubresourceFootprints.push_back(footprint);
            }
        }
        CachedImageDataMap[filename] = data;
        return data;
    }
    int channels = 0;
    const bool hdr = stbi_is_hdr(filename.ToStr());
    void* pixels = hdr ? (void*)stbi_loadf(filename.ToStr(), &data->Width, &data->Height, &channels, 4)
                       : (void*)stbi_load(filename.ToStr(), &data->Width, &data->Height, &channels, 4);
    if (!pixels) { std::cerr << "Image load failed: " << filename.ToStr() << ": " << stbi_failure_reason() << std::endl; return {}; }
    data->Format = hdr ? ETextureFormat::RGBA32F : ETextureFormat::RGBA8;
    data->FormatType = hdr ? EFormatType::FLOAT : EFormatType::UNSIGNED_BYTE;
    const size_t pitch = data->Width * 4 * (hdr ? sizeof(float) : 1);
    data->ImageBulkData.ImageData.resize(pitch * data->Height);
    std::memcpy(data->ImageBulkData.ImageData.data(), pixels, data->ImageBulkData.ImageData.size());
    stbi_image_free(pixels);
    data->ImageBulkData.SubresourceFootprints.resize(1);
    auto& footprint = data->ImageBulkData.SubresourceFootprints[0];
    footprint.Width = data->Width; footprint.Height = data->Height;
    footprint.RowPitch = pitch; footprint.Offset = 0; footprint.Depth = 0; footprint.MipLevel = 0;
    CachedImageDataMap[filename] = data;
    return data;
}
std::weak_ptr<jTexture> jImageFileLoader::LoadTextureFromFile(const jName& filename)
{
    auto it_find = CachedTextureMap.find(filename);
    if (CachedTextureMap.end() != it_find)
        return it_find->second;

    std::shared_ptr<jTexture> NewTexture;

    std::weak_ptr<jImageData> imageDataWeakPtr = LoadImageDataFromFile(filename);
    jImageData* pImageData = imageDataWeakPtr.lock().get();
    JASSERT(pImageData);
    if (pImageData)
    {
        NewTexture = g_rhi->CreateTextureFromData(pImageData);
    }

    CachedTextureMap[filename] = NewTexture;
    return NewTexture;
}
