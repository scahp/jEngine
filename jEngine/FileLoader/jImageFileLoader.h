#pragma once

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

	std::weak_ptr<jImageData> LoadImageDataFromFile(const jName& filename);
	std::weak_ptr<jTexture> LoadTextureFromFile(const jName& filename);

private:
	std::map<jName, std::shared_ptr<jImageData> > CachedImageDataMap;
	std::map<jName, std::shared_ptr<jTexture> > CachedTextureMap;
    jMutexLock CachedImageDataLock;

private:
	jImageFileLoader();

	static jImageFileLoader* _instance;
};

