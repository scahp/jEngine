#pragma once

struct jImageData
{
	bool srgb = false;
	int32 Width = 0;
	int32 Height = 0;
	std::string Filename;
	std::vector<unsigned char> ImageData;
};

class jImageFileLoader
{
public:
	~jImageFileLoader();

	static jImageFileLoader& GetInstance() { return *_instance; }

	void LoadTextureFromFile(jImageData& data, std::string const& filename);

private:
	jImageFileLoader();

	static jImageFileLoader* _instance;
};

