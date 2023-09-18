#include "pch.h"
#include "jShader_DX12.h"
#include "dxcapi.h"

jCompiledShader_DX12::~jCompiledShader_DX12()
{
    ShaderBlob.Reset();
    ShaderBlob = nullptr;
}
