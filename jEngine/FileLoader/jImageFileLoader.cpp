#include "pch.h"
#include "jImageFileLoader.h"
#include "jEditorAssetPath.h"
#include "lodepng.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "SOIL_STBI/stbi_DDS_aug_c.h"

#include "RHI/DX12/jBufferUtil_DX12.h"
#include "RHI/DX12/jTexture_DX12.h"


jImageFileLoader* jImageFileLoader::_instance = nullptr;

jImageFileLoader::jImageFileLoader()
{
}


jImageFileLoader::~jImageFileLoader()
{
}

std::weak_ptr<jImageData> jImageFileLoader::LoadImageDataFromFile(const jName& filename)
{
    return GetOrDecodeImage(filename);
}

std::shared_ptr<jImageData> jImageFileLoader::DecodeImageData(const jName& filename)
{
    char Ext[128] = "";
    _splitpath_s(filename.ToStr(), nullptr, 0, nullptr, 0, nullptr, 0, Ext, sizeof(Ext));
    for (int32 i = 0; i < _countof(Ext); ++i)
    {
        if (Ext[i] == '\0')
            break;
        Ext[i] = std::tolower(Ext[i]);
    }
    if (strlen(Ext) <= 0)
        return {};

    const jName ExtName(Ext);
    static jName ExtDDS(".dds");
    static jName ExtPNG(".png");
    static jName ExtHDR(".hdr");

    std::shared_ptr<jImageData> NewImageDataPatr = std::make_shared<jImageData>();
    NewImageDataPatr->Filename = filename;
    bool IsHDR = false;

    {
        const std::wstring FilenameWChar = jEditorAssetPath::FromUtf8(filename.ToStr()).native();

        DirectX::ScratchImage image;
        if (ExtName == ExtDDS)
        {
            if (FAILED(DirectX::LoadFromDDSFile(FilenameWChar.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image)))
                return {};
        }
        else if (ExtName == ExtHDR)
        {
            if (FAILED(DirectX::LoadFromHDRFile(FilenameWChar.c_str(), nullptr, image)))
                return {};
        }
        else
        {
            DirectX::ScratchImage imageOrigin;
            if (FAILED(DirectX::LoadFromWICFile(FilenameWChar.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, nullptr, imageOrigin)))
                return {};

            if (UseBlockCompressionForWIC)
            {
                DirectX::ScratchImage temp;
                int32 mipLevel = jTexture::GetMipLevels((int32)imageOrigin.GetMetadata().width, (int32)imageOrigin.GetMetadata().height);
                if (FAILED(DirectX::GenerateMipMaps(*imageOrigin.GetImages(), DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_SEPARATE_ALPHA, mipLevel, temp)))
                    return {};

                if (FAILED(DirectX::Compress(temp.GetImages(), temp.GetImageCount(),
                    temp.GetMetadata(), DXGI_FORMAT_BC3_UNORM, TEX_COMPRESS_DEFAULT, TEX_THRESHOLD_DEFAULT, image)))
                    return {};
            }
            else
            {
                int32 mipLevel = jTexture::GetMipLevels((int32)imageOrigin.GetMetadata().width, (int32)imageOrigin.GetMetadata().height);
                if (FAILED(DirectX::GenerateMipMaps(*imageOrigin.GetImages(), DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_SEPARATE_ALPHA, mipLevel, image)))
                    return {};
            }
        }

        if (image.GetMetadata().dimension != TEX_DIMENSION_TEXTURE2D
            || !image.GetImageCount() || !image.GetPixelsSize())
            return {};

        if (image.GetMetadata().IsCubemap())
        {
            NewImageDataPatr->TextureType = ETextureType::TEXTURE_CUBE;
        }
        else
        {
            switch (image.GetMetadata().dimension)
            {
            case TEX_DIMENSION_TEXTURE1D:
                check(image.GetMetadata().arraySize == 1);
                break;
            case TEX_DIMENSION_TEXTURE2D:
                if (image.GetMetadata().arraySize == 1)
                    NewImageDataPatr->TextureType = ETextureType::TEXTURE_2D;
                else if (image.GetMetadata().arraySize > 1)
                    NewImageDataPatr->TextureType = ETextureType::TEXTURE_2D_ARRAY;
                else
                    check(0);
                break;
            case TEX_DIMENSION_TEXTURE3D:
                if (image.GetMetadata().arraySize == 1)
                    NewImageDataPatr->TextureType = ETextureType::TEXTURE_3D;
                else if (image.GetMetadata().arraySize > 1)
                    NewImageDataPatr->TextureType = ETextureType::TEXTURE_3D_ARRAY;
                else
                    check(0);
                break;
            default:
                check(0);
                break;
            }
        }

        const uint64 numSubResources = image.GetMetadata().mipLevels * image.GetMetadata().arraySize;
        NewImageDataPatr->ImageBulkData.SubresourceFootprints.resize(numSubResources);
        // Canonical CPU representation, independent of the eventual GPU API.
        {
            for (int32 i = 0; i < numSubResources; ++i)
            {
                const Image& subImage = image.GetImages()[i];
                NewImageDataPatr->ImageBulkData.SubresourceFootprints[i].Depth = 0;
                NewImageDataPatr->ImageBulkData.SubresourceFootprints[i].Width = (uint32)subImage.width;
                NewImageDataPatr->ImageBulkData.SubresourceFootprints[i].Height = (uint32)subImage.height;
                NewImageDataPatr->ImageBulkData.SubresourceFootprints[i].RowPitch = (uint32)subImage.rowPitch;
                NewImageDataPatr->ImageBulkData.SubresourceFootprints[i].Format = image.GetMetadata().format;
                NewImageDataPatr->ImageBulkData.SubresourceFootprints[i].Offset = 0;
            }

            size_t SizeInBytes = 0;
            for (uint64 arrayIdx = 0; arrayIdx < image.GetMetadata().arraySize; ++arrayIdx)
            {
                for (uint64 mipIdx = 0; mipIdx < image.GetMetadata().mipLevels; ++mipIdx)
                {
                    const uint64 subResourceIdx = mipIdx + (arrayIdx * image.GetMetadata().mipLevels);
                    const Image& subImage = image.GetImages()[subResourceIdx];
                    // DirectXTex's slice pitch includes complete compressed
                    // blocks, including the 1x1 and 2x2 mip tails. A pixel-count
                    // estimate truncates BC3 tails and becomes zero for BC1.
                    SizeInBytes += subImage.slicePitch;
                }
            }

            NewImageDataPatr->ImageBulkData.ImageData.resize(SizeInBytes);
            size_t Offset = 0;
            for (uint64 arrayIdx = 0; arrayIdx < image.GetMetadata().arraySize; ++arrayIdx)
            {
                for (uint64 mipIdx = 0; mipIdx < image.GetMetadata().mipLevels; ++mipIdx)
                {
                    const uint64 subResourceIdx = mipIdx + (arrayIdx * image.GetMetadata().mipLevels);
                    const Image& subImage = image.GetImages()[subResourceIdx];

                    NewImageDataPatr->ImageBulkData.SubresourceFootprints[subResourceIdx].MipLevel = (uint32)mipIdx;
                    NewImageDataPatr->ImageBulkData.SubresourceFootprints[subResourceIdx].Depth = (uint32)arrayIdx;
                    NewImageDataPatr->ImageBulkData.SubresourceFootprints[subResourceIdx].Offset = Offset;

                    const size_t LocalSizeInByte = subImage.slicePitch;
                    memcpy(&NewImageDataPatr->ImageBulkData.ImageData[Offset], subImage.pixels, LocalSizeInByte);
                    Offset += LocalSizeInByte;
                }
            }
        }
        NewImageDataPatr->MipLevel = (int32)image.GetMetadata().mipLevels;
        NewImageDataPatr->LayerCount = (int32)image.GetMetadata().arraySize;
        NewImageDataPatr->Width = (int32)image.GetMetadata().width;
        NewImageDataPatr->Height = (int32)image.GetMetadata().height;
        NewImageDataPatr->Format = GetDX12TextureFormat(image.GetMetadata().format);
        NewImageDataPatr->FormatType = GetTexturePixelType(NewImageDataPatr->Format);
        NewImageDataPatr->sRGB = IsSRGB(image.GetMetadata().format);
        if (ExtName == ExtHDR)
        {
            IsHDR = true;
        }
    }

    return NewImageDataPatr;
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
    if (!g_rhi) return {};

    // Device-specific copy layouts belong to GPU finalization, never CPU decode.
    std::unique_ptr<jImageData> upload;
    const jImageData* textureData = imageData;
    if (!IsUseVulkan())
    {
        if (!g_rhi_dx12 || !g_rhi_dx12->Device || imageData->ImageBulkData.SubresourceFootprints.empty())
            return {};
        const auto& source = imageData->ImageBulkData;
        const uint32 count = static_cast<uint32>(source.SubresourceFootprints.size());
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(count);
        std::vector<uint32> rows(count);
        std::vector<uint64> rowSizes(count);
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = imageData->Width;
        desc.Height = imageData->Height;
        desc.DepthOrArraySize = static_cast<uint16>(imageData->LayerCount);
        desc.MipLevels = static_cast<uint16>(imageData->MipLevel);
        const auto sourceFormat = static_cast<DXGI_FORMAT>(source.SubresourceFootprints[0].Format);
        desc.Format = sourceFormat == DXGI_FORMAT_UNKNOWN ? GetDX12TextureFormat(imageData->Format) : sourceFormat;
        desc.SampleDesc.Count = 1;
        uint64 size = 0;
        g_rhi_dx12->Device->GetCopyableFootprints(&desc, 0, count, 0,
            layouts.data(), rows.data(), rowSizes.data(), &size);
        if (size == UINT64_MAX || size > SIZE_MAX)
            return {};
        upload = std::make_unique<jImageData>();
        upload->CreateMipmapIfPossible = imageData->CreateMipmapIfPossible;
        upload->sRGB = imageData->sRGB;
        upload->Width = imageData->Width;
        upload->Height = imageData->Height;
        upload->MipLevel = imageData->MipLevel;
        upload->LayerCount = imageData->LayerCount;
        upload->Filename = imageData->Filename;
        upload->Format = imageData->Format;
        upload->FormatType = imageData->FormatType;
        upload->TextureType = imageData->TextureType;
        upload->ImageBulkData.SubresourceFootprints = source.SubresourceFootprints;
        upload->ImageBulkData.ImageData.assign(static_cast<size_t>(size), 0);
        for (uint32 i = 0; i < count; ++i)
        {
            const auto& src = source.SubresourceFootprints[i];
            if (src.Offset > source.ImageData.size() || !src.RowPitch
                || rows[i] > (source.ImageData.size() - src.Offset) / src.RowPitch
                || rowSizes[i] > src.RowPitch)
                return {};
            auto& dst = upload->ImageBulkData.SubresourceFootprints[i];
            dst.Offset = layouts[i].Offset;
            dst.Width = layouts[i].Footprint.Width;
            dst.Height = layouts[i].Footprint.Height;
            dst.Depth = layouts[i].Footprint.Depth;
            dst.RowPitch = layouts[i].Footprint.RowPitch;
            dst.Format = layouts[i].Footprint.Format;
            for (uint32 row = 0; row < rows[i]; ++row)
                memcpy(upload->ImageBulkData.ImageData.data() + dst.Offset + size_t(row) * dst.RowPitch,
                    source.ImageData.data() + src.Offset + size_t(row) * src.RowPitch,
                    static_cast<size_t>(rowSizes[i]));
        }
        textureData = upload.get();
    }
    return g_rhi->CreateTextureFromData(textureData);
}
