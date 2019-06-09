#include "pch.h"
#include "jImageFileLoader.h"
#include "lodepng.h"

jImageFileLoader* jImageFileLoader::_instance = nullptr;

jImageFileLoader::jImageFileLoader()
{
}


jImageFileLoader::~jImageFileLoader()
{
}

void jImageFileLoader::LoadTextureFromFile(jImageData& data, std::string const& filename)
{
	if (std::string::npos != filename.find(".tga"))
	{
		// todo 
	}
	else if (std::string::npos != filename.find(".png"))
	{
		data.Filename = filename;
		unsigned w, h;
		LodePNG::decode(data.ImageData, w, h, filename.c_str());
		data.Width = static_cast<int32>(w);
		data.Height = static_cast<int32>(h);
	}
}
