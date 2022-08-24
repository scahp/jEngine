﻿#pragma once

#include "jGame.h"

class jEngine
{
public:
	jEngine();
	~jEngine();

	void Init();
	void Release();
	void ProcessInput();
	void Update(float deltaTime);
	void Draw();
	void Resize(int width, int height);
	void OnMouseButton();
	void OnMouseMove(int32 xOffset, int32 yOffset);

	jGame Game;

	class jAppSettingProperties* AppSettingProperties = nullptr;
};

