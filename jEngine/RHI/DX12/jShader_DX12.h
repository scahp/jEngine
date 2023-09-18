#pragma once
#include "Shader/jShader.h"

struct IDxcBlob;

struct jCompiledShader_DX12 : public jCompiledShader
{
    virtual ~jCompiledShader_DX12();

    ComPtr<IDxcBlob> ShaderBlob;
};
