#pragma once

class jMeshObject;

class jModelLoader
{
public:
	~jModelLoader();

	static jModelLoader& GetInstance()
	{
		if (!_instance)
			_instance = new jModelLoader();
		return *_instance;
	}

	jMeshObject* LoadFromFile(const char* filename);

private:
	jModelLoader();

	static jModelLoader* _instance;
};

