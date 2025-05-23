﻿#include "pch.h"
#include "jShaderCompiler_DX12.h"

//////////////////////////////////////////////////////////////////////////
// jDXC
jDXC::~jDXC()
{
	Release();
}

HRESULT jDXC::Initialize(const wchar_t* InDllName, const char* InFnName)
{
	if (m_dll)
		return S_OK;

	m_dll = LoadLibraryW(InDllName);

	if (!m_dll)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	m_createFn = (DxcCreateInstanceProc)(GetProcAddress(m_dll, InFnName));

	if (!m_createFn)
	{
		HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
		FreeLibrary(m_dll);
		m_dll = nullptr;
		return hr;
	}

	char fnName2[128];
	size_t len = strlen(InFnName);
	if (len < sizeof(fnName2) - 2)
	{
		memcpy(fnName2, InFnName, len);
		fnName2[len] = '2';
		fnName2[len + 1] = '\0';
		m_createFn2 = (DxcCreateInstance2Proc)(GetProcAddress(m_dll, fnName2));
	}

	return S_OK;
}

void jDXC::Release()
{
	if (m_dll)
	{
		m_createFn = nullptr;
		m_createFn2 = nullptr;
		FreeLibrary(m_dll);
		m_dll = nullptr;
	}
}

HRESULT jDXC::CreateInstance(REFCLSID InCLSID, REFIID InIID, IUnknown** pResult) const
{
	if (!pResult)
		return E_POINTER;

	if (!m_dll)
		return E_FAIL;

	if (!m_createFn)
		return E_FAIL;

	return m_createFn(InCLSID, InIID, (LPVOID*)pResult);
}

HRESULT jDXC::CreateInstance2(IMalloc* pMalloc, REFCLSID InCLSID, REFIID InIID, IUnknown** pResult) const
{
	if (!pResult)
		return E_POINTER;

	if (!m_dll)
		return E_FAIL;

	if (!m_createFn2)
		return E_FAIL;

	return m_createFn2(pMalloc, InCLSID, InIID, (LPVOID*)pResult);
}

HMODULE jDXC::Detach()
{
	HMODULE module = m_dll;
	m_dll = nullptr;
	return module;
}

//////////////////////////////////////////////////////////////////////////
// jShaderCompiler_DX12
jShaderCompiler_DX12* jShaderCompiler_DX12::_instance = nullptr;

HRESULT jShaderCompiler_DX12::Initialize()
{
	return m_dxc.Initialize();
}

ComPtr<IDxcBlob> jShaderCompiler_DX12::CompileFromFile(const wchar_t* InFilename, const wchar_t* InShadingModel
	, const wchar_t* InEntryPoint, bool InRowMajorMatrix) const
{
	if (!m_dxc.IsEnable())
		return nullptr;

	std::ifstream shaderFile(InFilename);
	if (!shaderFile.good())
		return nullptr;

	std::stringstream strStream;
	strStream << shaderFile.rdbuf();
	
	const std::string shader = strStream.str();
	return Compile(shader.c_str(), (uint32)shader.size(), InShadingModel, InEntryPoint, InRowMajorMatrix);
}

ComPtr<IDxcBlob> jShaderCompiler_DX12::Compile(const char* InShaderCode, uint32 InShaderCodeLength, const wchar_t* InShadingModel
	, const wchar_t* InEntryPoint, bool InRowMajorMatrix, std::vector<const wchar_t*> InCompileOptions) const
{
    if (!m_dxc.IsEnable())
        return nullptr;

	ComPtr<IDxcCompiler> Compiler;
    ComPtr<IDxcLibrary> Library;
    if (JFAIL(m_dxc.CreateInstance(CLSID_DxcCompiler, Compiler.GetAddressOf())))
        return nullptr;

    if (JFAIL(m_dxc.CreateInstance(CLSID_DxcLibrary, Library.GetAddressOf())))
        return nullptr;

    // Shader 로부터 Blob 생성
    ComPtr<IDxcBlobEncoding> textBlob;
    if (JFAIL(Library->CreateBlobWithEncodingFromPinned((LPBYTE)InShaderCode, InShaderCodeLength, 0, &textBlob)))
        return nullptr;

    std::vector<const wchar_t*> options;

	// DXC compile Option referece : https://simoncoenen.com/blog/programming/graphics/DxcCompiling, or 'dxc.exe -help'
    //options.push_back(TEXT("-WX"));				// Treat warnings as errors.

    if (InRowMajorMatrix)
        options.push_back(TEXT("-Zpr"));			// Pack matrices in row-major order.

#if _DEBUG
    options.push_back(TEXT("-Zi"));				// Debug info.
    options.push_back(TEXT("-Qembed_debug"));	// Embed PDB in shader container
    options.push_back(TEXT("-Od"));				// Disable optimization
#else
    options.push_back(TEXT("-O3"));				// Optimization Level 3 (Default)
#endif
	options.insert(options.end(), InCompileOptions.begin(), InCompileOptions.end());

    // Compile
    ComPtr<IDxcOperationResult> result;
    if (JFAIL(Compiler->Compile(textBlob.Get(), nullptr, InEntryPoint, InShadingModel
        , &options[0], (uint32)options.size(), nullptr, 0, nullptr, &result)))
    {
        return nullptr;
    }

    HRESULT resultCode;
    if (JFAIL(result->GetStatus(&resultCode)))
        return nullptr;

    if (FAILED(resultCode))
    {
        ComPtr<IDxcBlobEncoding> error;
        result->GetErrorBuffer(&error);
        auto tt = reinterpret_cast<const char*>(error->GetBufferPointer());
        OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
        return nullptr;
    }

    ComPtr<IDxcBlob> Blob;
    if (JFAIL(result->GetResult(&Blob)))
        return nullptr;

	return Blob;
}
