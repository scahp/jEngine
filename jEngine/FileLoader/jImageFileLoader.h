#pragma once

struct jImageData
{
	bool HasMipmap = false;
	bool CreateMipmapIfPossible = true;
	bool sRGB = false;
	int32 Width = 0;
	int32 Height = 0;
	jName Filename;
	std::vector<unsigned char> ImageData;
	ETextureFormat Format = ETextureFormat::RGBA8;
	EFormatType FormatType = EFormatType::UNSIGNED_BYTE;
	//ComPtr<ID3D12Resource> Resource;
	std::vector<jImageSubResourceData> SubresourceFootprints;
};

class jImageFileLoader
{
public:
	~jImageFileLoader();

	static jImageFileLoader& GetInstance()
	{
		if (!_instance)
			_instance = new jImageFileLoader();
		return *_instance; 
	}

	static void ReleaseInstance()
	{
		if (_instance)
		{
			_instance->CachedImageDataMap.clear();
			_instance->CachedTextureMap.clear();

			delete _instance;
			_instance = nullptr;
		}
	}

	std::weak_ptr<jImageData> LoadImageDataFromFile(const jName& filename, bool sRGB = false, bool paddingRGBA = false);
	std::weak_ptr<jTexture> LoadTextureFromFile(const jName& filename, bool sRGB = false, bool paddingRGBA = false);

private:
	std::map<jName, std::shared_ptr<jImageData> > CachedImageDataMap;
	std::map<jName, std::shared_ptr<jTexture> > CachedTextureMap;
    jMutexLock CachedImageDataLock;

private:
	jImageFileLoader();

	static jImageFileLoader* _instance;
};

