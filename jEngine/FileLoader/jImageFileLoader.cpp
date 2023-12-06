#include "pch.h"
#include "jImageFileLoader.h"
#include "lodepng.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "SOIL_STBI/stbi_DDS_aug_c.h"

#include "DirectXTex.h"
#include "RHI/DX12/jBufferUtil_DX12.h"
#include "RHI/DX12/jTexture_DX12.h"

#ifdef _DEBUG
#pragma comment(lib, "Debug/DirectXTex.lib")
#else
#pragma comment(lib, "Release/DirectXTex.lib")
#endif

jImageFileLoader* jImageFileLoader::_instance = nullptr;

jImageFileLoader::jImageFileLoader()
{
}


jImageFileLoader::~jImageFileLoader()
{
}

std::weak_ptr<jImageData> jImageFileLoader::LoadImageDataFromFile(const jName& filename)
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
        return std::weak_ptr<jImageData>();

    {
        jScopedLock s(&CachedImageDataLock);
        auto it_find = CachedImageDataMap.find(filename);
        if (CachedImageDataMap.end() != it_find)
            return it_find->second;
    }

    const jName ExtName(Ext);
    static jName ExtDDS(".dds");
    static jName ExtPNG(".png");
    static jName ExtHDR(".hdr");

    std::shared_ptr<jImageData> NewImageDataPatr = std::make_shared<jImageData>();
    bool IsHDR = false;

    {
        const int32 filenameLength = MultiByteToWideChar(CP_ACP, 0, filename.ToStr(), -1, NULL, NULL);
        check(filenameLength < 128);

        wchar_t FilenameWChar[128] = { 0, };
        MultiByteToWideChar(CP_ACP, 0, filename.ToStr()
            , (int32)filename.GetStringLength(), FilenameWChar, (int32)(_countof(FilenameWChar) - 1));
        FilenameWChar[_countof(FilenameWChar) - 1] = 0;

        DirectX::ScratchImage image;
        if (ExtName == ExtDDS)
        {
            if (JFAIL(DirectX::LoadFromDDSFile(FilenameWChar, DirectX::DDS_FLAGS_NONE, nullptr, image)))
                return std::weak_ptr<jImageData>();
        }
        else if (ExtName == ExtHDR)
        {
            if (JFAIL(DirectX::LoadFromHDRFile(FilenameWChar, nullptr, image)))
                return std::weak_ptr<jImageData>();
        }
        else
        {
            DirectX::ScratchImage imageOrigin;
            if (JFAIL(DirectX::LoadFromWICFile(FilenameWChar, DirectX::WIC_FLAGS_FORCE_RGB, nullptr, imageOrigin)))
                return std::weak_ptr<jImageData>();

            int32 mipLevel = jTexture::GetMipLevels((int32)imageOrigin.GetMetadata().width, (int32)imageOrigin.GetMetadata().height);
            DirectX::GenerateMipMaps(*imageOrigin.GetImages(), DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_SEPARATE_ALPHA, mipLevel, image);
        }

        check(image.GetMetadata().dimension == TEX_DIMENSION_TEXTURE2D);		// todo : 2D image 만 지원

        const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDescPtr = nullptr;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
        if (image.GetMetadata().IsCubemap())
        {
            check(image.GetMetadata().arraySize == 6);
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = uint32(image.GetMetadata().mipLevels);
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
            srvDescPtr = &srvDesc;
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
        NewImageDataPatr->SubresourceFootprints.resize(numSubResources);
        if (IsUseVulkan())
        {
            for (int32 i = 0; i < numSubResources; ++i)
            {
                const Image& subImage = image.GetImages()[i];
                NewImageDataPatr->SubresourceFootprints[i].Depth = 0;
                NewImageDataPatr->SubresourceFootprints[i].Width = (uint32)subImage.width;
                NewImageDataPatr->SubresourceFootprints[i].Height = (uint32)subImage.height;
                NewImageDataPatr->SubresourceFootprints[i].RowPitch = (uint32)subImage.rowPitch;
                NewImageDataPatr->SubresourceFootprints[i].Format = image.GetMetadata().format;
                NewImageDataPatr->SubresourceFootprints[i].Offset = 0;
            }

            size_t SizeInBytes = 0;
            for (uint64 arrayIdx = 0; arrayIdx < image.GetMetadata().arraySize; ++arrayIdx)
            {
                for (uint64 mipIdx = 0; mipIdx < image.GetMetadata().mipLevels; ++mipIdx)
                {
                    const uint64 subResourceIdx = mipIdx + (arrayIdx * image.GetMetadata().mipLevels);
                    const Image& subImage = image.GetImages()[subResourceIdx];
                    size_t LocalSizeInByte = subImage.width * subImage.height * (DirectX::BitsPerPixel(image.GetMetadata().format) / 8);
                    LocalSizeInByte = Align(LocalSizeInByte, subImage.slicePitch);
                    LocalSizeInByte = Align(LocalSizeInByte, subImage.rowPitch);
                    SizeInBytes += LocalSizeInByte;
                }
            }

            NewImageDataPatr->ImageData.resize(SizeInBytes);
            size_t Offset = 0;
            for (uint64 arrayIdx = 0; arrayIdx < image.GetMetadata().arraySize; ++arrayIdx)
            {
                for (uint64 mipIdx = 0; mipIdx < image.GetMetadata().mipLevels; ++mipIdx)
                {
                    const uint64 subResourceIdx = mipIdx + (arrayIdx * image.GetMetadata().mipLevels);
                    const Image& subImage = image.GetImages()[subResourceIdx];

                    NewImageDataPatr->SubresourceFootprints[subResourceIdx].MipLevel = (uint32)mipIdx;
                    NewImageDataPatr->SubresourceFootprints[subResourceIdx].Depth = (uint32)arrayIdx;
                    NewImageDataPatr->SubresourceFootprints[subResourceIdx].Offset = Offset;

                    size_t LocalSizeInByte = subImage.width * subImage.height * (DirectX::BitsPerPixel(image.GetMetadata().format) / 8);
                    memcpy(&NewImageDataPatr->ImageData[Offset], subImage.pixels, LocalSizeInByte);
                    LocalSizeInByte = Align(LocalSizeInByte, subImage.slicePitch);
                    LocalSizeInByte = Align(LocalSizeInByte, subImage.rowPitch);
                    Offset += LocalSizeInByte;
                }
            }
        }
        else
        {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts = (D3D12_PLACED_SUBRESOURCE_FOOTPRINT*)_alloca(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) * numSubResources);
            uint32* numRows = (uint32*)_alloca(sizeof(uint32) * numSubResources);
            uint64* rowSizes = (uint64*)_alloca(sizeof(uint64) * numSubResources);

            uint64 textureMemSize = 0;
            const bool is3D = image.GetMetadata().dimension == DirectX::TEX_DIMENSION_TEXTURE3D;

            D3D12_RESOURCE_DESC textureDesc = { };
            textureDesc.MipLevels = uint16(image.GetMetadata().mipLevels);
            textureDesc.Format = image.GetMetadata().format;
            textureDesc.Width = uint32(image.GetMetadata().width);
            textureDesc.Height = uint32(image.GetMetadata().height);
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            textureDesc.DepthOrArraySize = is3D ? uint16(image.GetMetadata().depth) : uint16(image.GetMetadata().arraySize);
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Dimension = is3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            textureDesc.Alignment = 0;
            g_rhi_dx12->Device->GetCopyableFootprints(&textureDesc, 0, uint32(numSubResources), 0, layouts, numRows, rowSizes, &textureMemSize);

            for (int32 i = 0; i < numSubResources; ++i)
            {
                NewImageDataPatr->SubresourceFootprints[i].Depth = layouts[i].Footprint.Depth;
                NewImageDataPatr->SubresourceFootprints[i].Width = layouts[i].Footprint.Width;
                NewImageDataPatr->SubresourceFootprints[i].Height = layouts[i].Footprint.Height;
                NewImageDataPatr->SubresourceFootprints[i].RowPitch = layouts[i].Footprint.RowPitch;
                NewImageDataPatr->SubresourceFootprints[i].Format = layouts[i].Footprint.Format;
                NewImageDataPatr->SubresourceFootprints[i].Offset = layouts[i].Offset;
            }

            NewImageDataPatr->ImageData.resize(textureMemSize);
            uint8* uploadMem = &NewImageDataPatr->ImageData[0];
            for (uint64 arrayIdx = 0; arrayIdx < image.GetMetadata().arraySize; ++arrayIdx)
            {
                for (uint64 mipIdx = 0; mipIdx < image.GetMetadata().mipLevels; ++mipIdx)
                {
                    const uint64 subResourceIdx = mipIdx + (arrayIdx * image.GetMetadata().mipLevels);
                    NewImageDataPatr->SubresourceFootprints[subResourceIdx].MipLevel = (uint32)mipIdx;

                    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& subResourceLayout = layouts[subResourceIdx];
                    const uint64 subResourceHeight = numRows[subResourceIdx];
                    const uint64 subResourcePitch = subResourceLayout.Footprint.RowPitch;
                    const uint64 subResourceDepth = subResourceLayout.Footprint.Depth;
                    uint8* dstSubResourceMem = reinterpret_cast<uint8*>(uploadMem) + subResourceLayout.Offset;

                    for (uint64 z = 0; z < subResourceDepth; ++z)
                    {
                        const DirectX::Image* subImage = image.GetImage(mipIdx, arrayIdx, z);
                        check(subImage != nullptr);
                        const uint8* srcSubResourceMem = subImage->pixels;

                        for (uint64 y = 0; y < subResourceHeight; ++y)
                        {
                            memcpy(dstSubResourceMem, srcSubResourceMem, Min(subResourcePitch, subImage->rowPitch));
                            dstSubResourceMem += subResourcePitch;
                            srcSubResourceMem += subImage->rowPitch;
                        }
                    }
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

    {
        jScopedLock s(&CachedImageDataLock);
        CachedImageDataMap[filename] = NewImageDataPatr;
    }
    return NewImageDataPatr;
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
        jTexture* pCreatedTexture = g_rhi->CreateTextureFromData(pImageData);
        NewTexture = std::shared_ptr<jTexture>(pCreatedTexture);
    }

    CachedTextureMap[filename] = NewTexture;
    return NewTexture;
}
