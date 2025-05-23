/*

Copyright (c) 2018 Miles Macklin

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgement in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

*/

/*
    This is a modified version of the original code
    Link to original code: https://github.com/mmacklin/tinsel
*/

// This is moidifed version of the https://github.com/knightcrawler25/GLSL-PathTracer repository.

#pragma once

#include "jPathTracingData.h"

class jPathTracingLoadData;
namespace GLSLPT
{
    bool LoadSceneFromFile(const std::string& filename, jPathTracingLoadData* scene, jRenderOptions& renderOptions);
}