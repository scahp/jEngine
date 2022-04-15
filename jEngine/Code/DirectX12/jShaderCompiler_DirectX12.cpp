#include "pch.h"
#include "jShaderCompiler_DirectX12.h"

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
// jShaderCompiler_DirectX12
jShaderCompiler_DirectX12* jShaderCompiler_DirectX12::_instance = nullptr;

HRESULT jShaderCompiler_DirectX12::Initialize()
{
	return m_dxc.Initialize();
}

ComPtr<IDxcBlob> jShaderCompiler_DirectX12::Compile(const wchar_t* InFilename, const wchar_t* InTargetString) const
{
	if (!m_dxc.IsEnable())
		return nullptr;

	ComPtr<IDxcCompiler> Compiler;
	ComPtr<IDxcLibrary> Library;
	if (JFAIL(m_dxc.CreateInstance(CLSID_DxcCompiler, Compiler.GetAddressOf())))
		return nullptr;

	if (JFAIL(m_dxc.CreateInstance(CLSID_DxcLibrary, Library.GetAddressOf())))
		return nullptr;

	std::ifstream shaderFile(InFilename);
	if (!shaderFile.good())
		return nullptr;

	std::stringstream strStream;
	strStream << shaderFile.rdbuf();
	
	std::string shader = strStream.str();

	// Shader 로부터 Blob 생성
	ComPtr<IDxcBlobEncoding> textBlob;
	if (JFAIL(Library->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32)shader.size(), 0, &textBlob)))
		return nullptr;

	std::vector<const wchar_t*> options;
	options.push_back(TEXT("-WX"));				// Treat warnings as errors.
	options.push_back(TEXT("-Zpr"));			// Pack matrices in row-major order.

#ifdef _DEBUG
	options.push_back(TEXT("-Zi"));				// Debug info.
	options.push_back(TEXT("-Qembed_debug"));	// Embed PDB in shader container
	options.push_back(TEXT("-Od"));				// Disable optimization
#else
	options.push_back(TEXT("-O3"));				// Optimization Level 3 (Default)
#endif

	// Compile
	ComPtr<IDxcOperationResult> result;
	if (JFAIL(Compiler->Compile(textBlob.Get(), InFilename, TEXT(""), InTargetString
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
		OutputDebugStringA(reinterpret_cast<const char*>(error->GetBufferPointer()));
        JFAIL(resultCode);
        return nullptr;
	}

	ComPtr<IDxcBlob> Blob;
	if (JFAIL(result->GetResult(&Blob)))
		return nullptr;

	return Blob;
}
