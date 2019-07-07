// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

#ifndef PCH_H
#define PCH_H

#include <windows.h>

// TODO: add headers that you want to pre-compile here
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <assert.h>

#include <vector>
#include <list>
#include <memory>
#include <string>
#include <stdexcept>
#include <stdlib.h>
#include <map>
#include <array>
#include <functional>
#include <sstream>
#include <set>
#include <algorithm>
#include <unordered_map>


#define JASSERT(x) assert(x)
#define JMESSAGE(x) MessageBoxA(0, x, "", MB_OK)

using int8 = char;
using uint8 = unsigned char;
using int16 = short;
using uint16 = unsigned short;
using int32 = int;
using uint32 = unsigned int;
using int64 = __int64;
using uint64 = unsigned __int64;

using tchar = wchar_t;

// settings
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

const unsigned int SM_WIDTH = 512;
const unsigned int SM_HEIGHT = 512;

#define FORCEINLINE __forceinline

enum class EMouseButtonType
{
	LEFT = 0,
	MIDDLE,
	RIGHT,
	MAX
};

extern std::map<int, bool> g_KeyState;
extern std::map<EMouseButtonType, bool> g_MouseState;

//////////////////////////////////////////////////////////////////////////
// https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
template <class T>
inline void hash_combine(std::size_t& seed, const T& v)
{
	std::hash<T> hasher;
	seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

#include "jRHIType.h"

ERenderBufferType MakeRenderBufferTypeList(const std::initializer_list<ERenderBufferType>& list);


#endif //PCH_H
