#pragma once

#include "jGame.h"

class jEngine
{
public:
	jEngine();
	~jEngine();

	void PreInit();
	void Init();
	void Release();
	void ProcessInput(float deltaTime);
	void Update(float deltaTime);
	void Draw();
	void Resize(int32 width, int32 height);
	void OnMouseButton();
	void OnMouseMove(int32 xOffset, int32 yOffset);

	jGame Game;

	class jAppSettingProperties* AppSettingProperties = nullptr;
};

