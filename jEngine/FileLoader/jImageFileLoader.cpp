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

std::weak_ptr<jImageData> jImageFileLoader::LoadImageDataFromFile(const jName& filename, bool sRGB, bool paddingRGBA)
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

	auto it_find = CachedImageDataMap.find(filename);
	if (CachedImageDataMap.end() != it_find)
		return it_find->second;

	const jName ExtName(Ext);
	static jName ExtDDS(".dds");
	static jName ExtPNG(".png");
	static jName ExtHDR(".hdr");

	std::shared_ptr<jImageData> NewImageDataPatr = std::make_shared<jImageData>();
	bool IsHDR = false;

	int32 width = 0;
	int32 height = 0;
	int32 NumOfComponent = -1;
	if (ExtName == ExtDDS)
	{
		if (IsUseVulkan())
		{
			uint8* imageData = stbi_dds_load(filename.ToStr(), &width, &height, &NumOfComponent, paddingRGBA ? STBI_rgb_alpha : 0);
			if (paddingRGBA)
				NumOfComponent = 4;

			int32 NumOfBytes = width * height * sizeof(uint8) * NumOfComponent;
			NewImageDataPatr->ImageData.resize(NumOfBytes);
			memcpy(&NewImageDataPatr->ImageData[0], imageData, NumOfBytes);
			NewImageDataPatr->sRGB = sRGB;
			NewImageDataPatr->FormatType = EFormatType::UNSIGNED_BYTE;

			stbi_image_free(imageData);
		}
		else
		{
			const int32 filenameLength = MultiByteToWideChar(CP_ACP, 0, filename.ToStr(), -1, NULL, NULL);
			check(filenameLength < 128);

			wchar_t FilenameWChar[128] = { 0, };
			MultiByteToWideChar(CP_ACP, 0, filename.ToStr()
				, (int32)filename.GetStringLength(), FilenameWChar, (int32)(_countof(FilenameWChar) - 1));
			FilenameWChar[_countof(FilenameWChar) - 1] = 0;

			DirectX::ScratchImage image;
			if (JFAIL(DirectX::LoadFromDDSFile(FilenameWChar, DirectX::DDS_FLAGS_NONE, nullptr, image)))
				return std::weak_ptr<jImageData>();

			check(image.GetMetadata().dimension == TEX_DIMENSION_TEXTURE2D);		// todo : 2D image 만 지원

			width = (int32)image.GetMetadata().width;
			height = (int32)image.GetMetadata().height;
			NumOfComponent = GetDX12TextureComponentCount(GetDX12TextureFormat(image.GetMetadata().format));
			if (image.GetMetadata().mipLevels > 1)
				NewImageDataPatr->HasMipmap = true;

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

			static D3D12_HEAP_PROPERTIES DefaultHeapProp{ .Type = D3D12_HEAP_TYPE_DEFAULT, .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN
				, .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN, .CreationNodeMask = 0, .VisibleNodeMask = 0 };

			ComPtr<ID3D12Resource> Resource = nullptr;
			JFAIL(g_rhi_dx12->Device->CreateCommittedResource(&DefaultHeapProp, D3D12_HEAP_FLAG_NONE, &textureDesc,
				D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&Resource)));

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
			}

			const uint64 numSubResources = image.GetMetadata().mipLevels * image.GetMetadata().arraySize;
			NewImageDataPatr->SubresourceFootprints.resize(numSubResources);
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT* layouts = (D3D12_PLACED_SUBRESOURCE_FOOTPRINT*)_alloca(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) * numSubResources);
			uint32* numRows = (uint32*)_alloca(sizeof(uint32) * numSubResources);
			uint64* rowSizes = (uint64*)_alloca(sizeof(uint64) * numSubResources);

			uint64 textureMemSize = 0;
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

			NewImageDataPatr->Width = width;
			NewImageDataPatr->Height = height;
			NewImageDataPatr->sRGB = sRGB;
			NewImageDataPatr->FormatType = EFormatType::UNSIGNED_BYTE;
			NewImageDataPatr->Format = GetDX12TextureFormat(image.GetMetadata().format);
		}
	}
	else if (ExtName == ExtPNG)
	{
		NewImageDataPatr->Filename = filename;
		unsigned w, h;
		LodePNG::decode(NewImageDataPatr->ImageData, w, h, filename.ToStr());
		NewImageDataPatr->sRGB = sRGB;
		NewImageDataPatr->FormatType = EFormatType::UNSIGNED_BYTE;

		width = static_cast<int32>(w);
		height = static_cast<int32>(h);
		NumOfComponent = 4;
	}
	else if (ExtName == ExtHDR)
	{
		float* imageData = stbi_loadf(filename.ToStr(), &width, &height, &NumOfComponent, paddingRGBA ? STBI_rgb_alpha : 0);
        if (paddingRGBA)
            NumOfComponent = 4;

		int32 NumOfBytes = width * height * sizeof(float) * NumOfComponent;
		NewImageDataPatr->ImageData.resize(NumOfBytes);
		memcpy(&NewImageDataPatr->ImageData[0], imageData, NumOfBytes);
		NewImageDataPatr->sRGB = sRGB;
		NewImageDataPatr->FormatType = EFormatType::FLOAT;

		stbi_image_free(imageData);
		IsHDR = true;
	}
	else
	{
		uint8* imageData = stbi_load(filename.ToStr(), &width, &height, &NumOfComponent, paddingRGBA ? STBI_rgb_alpha : 0);
        if (paddingRGBA)
            NumOfComponent = 4;

		int32 NumOfBytes = width * height * sizeof(uint8) * NumOfComponent;
		NewImageDataPatr->ImageData.resize(NumOfBytes);
		memcpy(&NewImageDataPatr->ImageData[0], imageData, NumOfBytes);
		NewImageDataPatr->sRGB = sRGB;
		NewImageDataPatr->FormatType = EFormatType::UNSIGNED_BYTE;

		stbi_image_free(imageData);
	}

	NewImageDataPatr->Width = width;
	NewImageDataPatr->Height = height;

	switch (NumOfComponent)
	{
	case 1:
		NewImageDataPatr->Format = IsHDR ? ETextureFormat::R32F : ETextureFormat::R8;
		break;
	case 2:
		NewImageDataPatr->Format = IsHDR ? ETextureFormat::RG32F : ETextureFormat::RG8;
		break;
	case 3:
		NewImageDataPatr->Format = IsHDR ? ETextureFormat::RGB32F : ETextureFormat::RGB8;
		break;
	case 4:
		NewImageDataPatr->Format = IsHDR ? ETextureFormat::RGBA32F : ETextureFormat::RGBA8;
		break;
	default:
		if (NewImageDataPatr->SubresourceFootprints.size() <= 0)
		{
			check(0);
		}
		break;
	}

	CachedImageDataMap[filename] = NewImageDataPatr;
	return NewImageDataPatr;
}

std::weak_ptr<jTexture> jImageFileLoader::LoadTextureFromFile(const jName& filename, bool sRGB, bool paddingRGBA)
{
	auto it_find = CachedTextureMap.find(filename);
	if (CachedTextureMap.end() != it_find)
		return it_find->second;

	std::shared_ptr<jTexture> NewTexture;

	std::weak_ptr<jImageData> imageDataWeakPtr = LoadImageDataFromFile(filename, sRGB, paddingRGBA);
	jImageData* pImageData = imageDataWeakPtr.lock().get();
	JASSERT(pImageData);
	if (pImageData)
	{
		if (pImageData->SubresourceFootprints.empty())
		{
            jTexture* pCreatedTexture = g_rhi->CreateTextureFromData(&pImageData->ImageData[0], pImageData->Width
                , pImageData->Height, pImageData->sRGB, pImageData->Format, true);
            NewTexture = std::shared_ptr<jTexture>(pCreatedTexture);
		}
		else
		{
			jTexture* pCreatedTexture = g_rhi->CreateTextureFromData(&pImageData->ImageData[0], (int32)pImageData->ImageData.size(), pImageData->Width
				, pImageData->Height, pImageData->HasMipmap, pImageData->Format, pImageData->SubresourceFootprints);
			NewTexture = std::shared_ptr<jTexture>(pCreatedTexture);
		}
	}

	CachedTextureMap[filename] = NewTexture;
	return NewTexture;
}
