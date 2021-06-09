#pragma once
#include "jRHI.h"

#include <windows.h>

class jRHI_DirectX12 : public jRHI
{
public:
	void Initialize();

	HWND CreateMainWindow() const;

};

