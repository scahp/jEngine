#pragma once
#include "dxcapi.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

//////////////////////////////////////////////////////////////////////////
// jDXC
class jDXC
{
public:
	jDXC() {}
	jDXC(jDXC&& InOther)
	{
		m_dll = InOther.m_dll;
		InOther.m_dll = nullptr;

		m_createFn = InOther.m_createFn;
		InOther.m_createFn = nullptr;

		m_createFn2 = InOther.m_createFn2;
		InOther.m_createFn2 = nullptr;
	}
	~jDXC();

	HRESULT Initialize() { return Initialize(TEXT("dxcompiler.dll"), "DxcCreateInstance"); }
	HRESULT Initialize(const wchar_t* InDllName, const char* InFnName);
	void Release();

	template <typename T>
	HRESULT CreateInstance(REFCLSID InCLSID, T** pResult) const
	{
		return CreateInstance(InCLSID, __uuidof(T), (IUnknown**)pResult);
	}

	template <typename T>
	HRESULT CreateInstance2(IMalloc* pMalloc, REFCLSID InCLSID, T** pResult) const
	{
		return CreateInstance2(pMalloc, InCLSID, __uuidof(T), (IUnknown**)pResult);
	}

	HRESULT CreateInstance(REFCLSID InCLSID, REFIID InIID, IUnknown** pResult) const;
	HRESULT CreateInstance2(IMalloc* pMalloc, REFCLSID InCLSID, REFIID InIID, IUnknown** pResult) const;

	bool HasCreateWithMalloc() const { return m_createFn2; }
	bool IsEnable() const { return m_dll; }
	HMODULE Detach();

private:

private:
	HMODULE m_dll = nullptr;
	DxcCreateInstanceProc m_createFn = nullptr;
	DxcCreateInstance2Proc m_createFn2 = nullptr;
};

//////////////////////////////////////////////////////////////////////////
// jShaderCompiler_DirectX12
class jShaderCompiler_DirectX12
{
public:
	static jShaderCompiler_DirectX12& Get()
	{
		if (!_instance)
		{
			_instance = new jShaderCompiler_DirectX12();
			_instance->Initialize();
		}
		return *_instance;
	}
	static void Destroy()
	{
		delete _instance;
	}

	HRESULT Initialize();
	ComPtr<ID3DBlob> Compile(const wchar_t* InFilename, const wchar_t* InTargetString) const;

public:
	jShaderCompiler_DirectX12(jShaderCompiler_DirectX12 const&) = delete;
	jShaderCompiler_DirectX12& operator=(jShaderCompiler_DirectX12 const&) = delete;
	jShaderCompiler_DirectX12(jShaderCompiler_DirectX12&&) = delete;
	jShaderCompiler_DirectX12& operator=(jShaderCompiler_DirectX12&&) = delete;

private:
	jDXC m_dxc;

private:
	jShaderCompiler_DirectX12() {}
	static jShaderCompiler_DirectX12* _instance;
};
