#pragma once

#include "jRHI.h"

#include <windows.h>


// todo 
class jRHI_DirectX11 : public jRHI
{
public:
	jRHI_DirectX11();
	~jRHI_DirectX11();

	void Initialize();

	HWND CreateMainWindow() const;

};

