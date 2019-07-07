// pch.cpp: source file corresponding to pre-compiled header; necessary for compilation to succeed

#include "pch.h"
#include "jRHI.h"

// In general, ignore this file, but keep it around if you are using pre-compiled headers.

std::map<int, bool> g_KeyState;
std::map<EMouseButtonType, bool> g_MouseState;

ERenderBufferType MakeRenderBufferTypeList(const std::initializer_list<ERenderBufferType>& list)
{
	uint32 result = 0;
	for (auto it = std::begin(list); std::end(list) != it; ++it)
	{
		result |= static_cast<uint32>(*it);
	}
	return static_cast<ERenderBufferType>(result);
}
