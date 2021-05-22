#include "pch.h"
#include "jRHI_DirectX11.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <directxmath.h>
using namespace DirectX;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

IDXGISwapChain* SwapChain = nullptr;
ID3D11Device* Device = nullptr;
ID3D11DeviceContext* DeviceContext = nullptr;
ID3D11RenderTargetView* RenderTargetView = nullptr;
ID3D11Texture2D* DepthStencilBuffer = nullptr;
ID3D11DepthStencilState* DepthStencilState = nullptr;
ID3D11DepthStencilView* DepthStencilView = nullptr;
ID3D11RasterizerState* RasterState = nullptr;
XMMATRIX ProjectionMatrix;
XMMATRIX WorldMatrix;
XMMATRIX OrthoMatrix;
bool InFullScreen = false;
bool EnableVsync = false;

ID3D11Buffer* VertexBuffer = nullptr;
ID3D11Buffer* IndexBuffer = nullptr;
ID3D11VertexShader* VertexShader = nullptr;
ID3D11PixelShader* PixelShader = nullptr;
ID3D11InputLayout* Layout = nullptr;
ID3D11Buffer* MatrixBuffer = nullptr;

struct VertexType
{
	XMFLOAT3 Position;
	XMFLOAT4 Color;
};

struct MatrixBufferType
{
	XMMATRIX World;
	XMMATRIX View;
	XMMATRIX Projection;
};


bool InitializeBuffers(ID3D11Device* InDevice)
{
	int VertexCount = 3;
	int IndexCount = 3;

	VertexType* Vertices = new VertexType[VertexCount];
	if (!Vertices)
		return false;

	unsigned long* Indices = new unsigned long[IndexCount];
	if (!Indices)
	{
		delete []Vertices;
		return false;
	}

	// 정점 배열에 데이터를 설정합니다.
	Vertices[0].Position = XMFLOAT3(-1.0f, -1.0f, 0.0f);  // Bottom left.
	Vertices[0].Color = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);

	Vertices[1].Position = XMFLOAT3(0.0f, 1.0f, 0.0f);  // Top middle.
	Vertices[1].Color = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);

	Vertices[2].Position = XMFLOAT3(1.0f, -1.0f, 0.0f);  // Bottom right.
	Vertices[2].Color = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);

	Indices[0] = 0;
	Indices[1] = 1;
	Indices[2] = 2;

	D3D11_BUFFER_DESC VertexBufferDesc;
	VertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	VertexBufferDesc.ByteWidth = sizeof(VertexType) * VertexCount;
	VertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	VertexBufferDesc.CPUAccessFlags = 0;
	VertexBufferDesc.MiscFlags = 0;
	VertexBufferDesc.StructureByteStride = 0;

	// Subresource 구조에 정점 데이터에 대한 포인터 제공
	D3D11_SUBRESOURCE_DATA VertexData;
	VertexData.pSysMem = Vertices;
	VertexData.SysMemPitch = 0;
	VertexData.SysMemSlicePitch = 0;

	if (FAILED(InDevice->CreateBuffer(&VertexBufferDesc, &VertexData, &VertexBuffer)))
	{
		delete []Vertices;
		delete []Indices;
		return false;
	}

	D3D11_BUFFER_DESC IndexBufferDesc;
	IndexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	IndexBufferDesc.ByteWidth = sizeof(unsigned long) * IndexCount;
	IndexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	IndexBufferDesc.CPUAccessFlags = 0;
	IndexBufferDesc.MiscFlags = 0;
	IndexBufferDesc.StructureByteStride = 0;

	// Subresource 구조에 인덱스 데이터에 대한 포인터 제공
	D3D11_SUBRESOURCE_DATA IndexData;
	IndexData.pSysMem = Indices;
	IndexData.SysMemPitch = 0;
	IndexData.SysMemSlicePitch = 0;

	if (FAILED(InDevice->CreateBuffer(&IndexBufferDesc, &IndexData, &IndexBuffer)))
	{
		delete []Vertices;
		delete []Indices;
		VertexBuffer->Release();
		return false;
	}

	delete []Vertices;
	delete []Indices;

	return true;
}

bool InitializeShader(ID3D11Device* InDevice, HWND InHwnd, const WCHAR* InVSFilename, const WCHAR* InPSFilename)
{
	ID3D10Blob* ErrorMessage = nullptr;

	// Compile VS
	ID3D10Blob* VertexShaderBuffer = nullptr;
	if (FAILED(D3DCompileFromFile(InVSFilename, nullptr, nullptr, "ColorVertexShader", "vs_5_0"
		, D3D10_SHADER_ENABLE_STRICTNESS, 0, &VertexShaderBuffer, &ErrorMessage)))
	{
		if (ErrorMessage)
			;// OutputShaderErrorMessage(ErrorMessage, InHwnd, InVSFilename);
		else
			MessageBox(InHwnd, InVSFilename, TEXT("Missing Shader File"), MB_OK);

		return false;
	}

	ID3D10Blob* PixelShaderBuffer = nullptr;
	if (FAILED(D3DCompileFromFile(InPSFilename, nullptr, nullptr, "ColorPixelShader", "ps_5_0"
		, D3D10_SHADER_ENABLE_STRICTNESS, 0, &PixelShaderBuffer, &ErrorMessage)))
	{
		if (ErrorMessage)
			;// OutputShaderErrorMessage(ErrorMessage, InHwnd, InVSFilename);
		else
			MessageBox(InHwnd, InVSFilename, TEXT("Missing Shader File"), MB_OK);

		return false;
	}

	if (FAILED(InDevice->CreateVertexShader(VertexShaderBuffer->GetBufferPointer(), VertexShaderBuffer->GetBufferSize(), nullptr, &VertexShader)))
		return false;

	if (FAILED(InDevice->CreatePixelShader(PixelShaderBuffer->GetBufferPointer(), PixelShaderBuffer->GetBufferSize(), nullptr, &PixelShader)))
		return false;

	// 정점 입력 레이아웃 구조체 설정
	// 이 설정은 ModelClass와 쉐이더의 VertexType 구조와 일치해야 합니다.
	D3D11_INPUT_ELEMENT_DESC PolygonLayout[2];
	PolygonLayout[0].SemanticName = "POSITION";
	PolygonLayout[0].SemanticIndex = 0;
	PolygonLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	PolygonLayout[0].InputSlot = 0;
	PolygonLayout[0].AlignedByteOffset = 0;
	PolygonLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	PolygonLayout[0].InstanceDataStepRate = 0;

	PolygonLayout[1].SemanticName = "COLOR";
	PolygonLayout[1].SemanticIndex = 0;
	PolygonLayout[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	PolygonLayout[1].InputSlot = 0;
	PolygonLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	PolygonLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	PolygonLayout[1].InstanceDataStepRate = 0;

	unsigned int NumElements = sizeof(PolygonLayout) / sizeof(PolygonLayout[0]);

	// 정점 입력 레이아웃을 만듬
	if (FAILED(InDevice->CreateInputLayout(PolygonLayout, NumElements, VertexShaderBuffer->GetBufferPointer(), VertexShaderBuffer->GetBufferSize(), &Layout)))
		return false;

	// 더이상 사용하지 않는 버퍼들 제거
	VertexShaderBuffer->Release();
	PixelShaderBuffer->Release();

	// 정점 쉐이더에 있는 행렬 상수 버퍼의 구조체 작성
	D3D11_BUFFER_DESC MatrixBufferDesc;
	MatrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	MatrixBufferDesc.ByteWidth = sizeof(MatrixBufferType);
	MatrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	MatrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	MatrixBufferDesc.MiscFlags = 0;
	MatrixBufferDesc.StructureByteStride = 0;

	if (FAILED(InDevice->CreateBuffer(&MatrixBufferDesc, nullptr, &MatrixBuffer)))
		return false;

	return true;
}

bool SetShaderParameters(ID3D11DeviceContext* InDeviceContext, const XMMATRIX& InWorldMatrix
	, const XMMATRIX& InViewMatrix, const XMMATRIX& InProjectionMatrix)
{
	// 행렬을 Transpose 하여 쉐이더에서 사용할 수 있게 합니다.
	auto WorldMatrix = XMMatrixTranspose(InWorldMatrix);
	auto ViewMatrix = XMMatrixTranspose(InViewMatrix);
	auto ProjectionMatrix = XMMatrixTranspose(InProjectionMatrix);

	D3D11_MAPPED_SUBRESOURCE MappedResource;
	if (FAILED(InDeviceContext->Map(MatrixBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource)))
		return false;

	MatrixBufferType* DataPtr = (MatrixBufferType*)MappedResource.pData;
	DataPtr->World = WorldMatrix;
	DataPtr->View = ViewMatrix;
	DataPtr->Projection = ProjectionMatrix;

	InDeviceContext->Unmap(MatrixBuffer, 0);

	unsigned int BufferNumber = 0;
	InDeviceContext->VSSetConstantBuffers(BufferNumber, 1, &MatrixBuffer);	// 정점쉐이더에서의 상수 버퍼 위치 설정

	return true;
}

XMMATRIX CreateViewMatrix()
{
	auto Position = XMFLOAT3(0.0f, 0.0f, -4.0f);
	auto Rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);

	XMFLOAT3 Up, Pos, LookAt;
	XMVECTOR UpVector, PositionVector, LookAtVector;
	float Yaw, Pitch, Roll;
	XMMATRIX RotationMatrix;

	Up.x = 0.0f;
	Up.y = 1.0f;
	Up.x = 0.0f;
	UpVector = XMLoadFloat3(&Up);

	Pos = Position;
	PositionVector = XMLoadFloat3(&Pos);

	LookAt.x = 0.0f;
	LookAt.y = 0.0f;
	LookAt.z = 1.0f;
	LookAtVector = XMLoadFloat3(&LookAt);

	static float temp = 0.0174532925f;
	Pitch = Rotation.x * temp;
	Yaw = Rotation.y * temp;
	Roll = Rotation.z * temp;

	RotationMatrix = XMMatrixRotationRollPitchYaw(Pitch, Yaw, Roll);

	LookAtVector = XMVector3TransformCoord(LookAtVector, RotationMatrix);
	UpVector = XMVector3TransformCoord(UpVector, RotationMatrix);
	LookAtVector = XMVectorAdd(PositionVector, LookAtVector);
	auto ViewMatrix = XMMatrixLookAtLH(PositionVector, LookAtVector, UpVector);

	return ViewMatrix;
}

LRESULT CALLBACK WindowProcDX11(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
	{
		// Save the DXSample* passed in to CreateWindow.
		LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
	}
	return 0;

	case WM_KEYDOWN:
		return 0;

	case WM_KEYUP:
		return 0;

	case WM_PAINT:
	{
		const float ClearColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

		DeviceContext->ClearRenderTargetView(RenderTargetView, ClearColor);
		DeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

		unsigned int Stride = sizeof(VertexType);
		unsigned int Offset = 0;

		DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
		DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
		DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		SetShaderParameters(DeviceContext, WorldMatrix, CreateViewMatrix(), ProjectionMatrix);

		DeviceContext->IASetInputLayout(Layout);

		DeviceContext->VSSetShader(VertexShader, nullptr, 0);
		DeviceContext->PSSetShader(PixelShader, nullptr, 0);

		int IndexCount = 3;
		DeviceContext->DrawIndexed(IndexCount, 0, 0);

		if (EnableVsync)
			SwapChain->Present(1, 0);	// 화면 새로고침 비율을 고정
		else
			SwapChain->Present(0, 0);	// 가능한 빠르게 출력
	}
	return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

jRHI_DirectX11::jRHI_DirectX11()
{
}


jRHI_DirectX11::~jRHI_DirectX11()
{
}

void jRHI_DirectX11::Initialize()
{
	auto hInstance = GetModuleHandle(NULL);

	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProcDX11;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"DXSampleClass";
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, static_cast<LONG>(SCR_WIDTH), static_cast<LONG>(SCR_HEIGHT) };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	auto hWnd = CreateWindow(
		windowClass.lpszClassName,
		TEXT("DX11"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,        // We have no parent window.
		nullptr,        // We aren't using menus.
		hInstance,
		nullptr);

	//////////////////////////////////////////////////////////////////////////
	IDXGIFactory* Factory = nullptr;
	if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&Factory)))
		return;

	// Create GraphicCard Interface Adapter by using Factory
	IDXGIAdapter* Adapter = nullptr;
	if (FAILED(Factory->EnumAdapters(0, &Adapter)))
		return;

	// 출력(모니터)에 대한 첫번째 어뎁터를 지정합니다.
	IDXGIOutput* AdapterOutput = nullptr;
	if (FAILED(Adapter->EnumOutputs(0, &AdapterOutput)))
		return;

	// 출력(모니터)에 대한 DXGI_FORMAT_R8G8B8A8_UNORM 표시 형식에 맞는 모드 수를 가져옴
	unsigned int NumModes = 0;
	if (FAILED(AdapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED,
		&NumModes, NULL)))
	{
		return;
	}

	// 가능한 모든 모니터와 그래픽카드 조합을 저장할 리스트를 생성합니다.
	DXGI_MODE_DESC* DisplayModeList = new DXGI_MODE_DESC[NumModes];
	if (!DisplayModeList)
		return;

	// 이제 디스플레이 모드에 대한 리스트를 채웁니다.
	if (FAILED(AdapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM
		, DXGI_ENUM_MODES_INTERLACED, &NumModes, DisplayModeList)))
	{
		return;
	}

	// 이제 모든 디스플레이 모드에 대해 화면 너비/높이에 맞는 디스플레이 모드를 찾습니다.
// 적합한 것을 찾으면 모니터의 새로고침 비율의 분모와 분자 값을 저장합니다.
	unsigned int Numerator = 0;
	unsigned int Denominator = 0;
	for (unsigned int i = 0; i < NumModes; ++i)
	{
		const auto& CurDisplayMode = DisplayModeList[i];
		if ((CurDisplayMode.Width == (unsigned int)SCR_WIDTH)
			&& (CurDisplayMode.Height == (unsigned int)SCR_HEIGHT))
		{
			Numerator = CurDisplayMode.RefreshRate.Numerator;
			Denominator = CurDisplayMode.RefreshRate.Denominator;
		}
	}

	// 비디오카드의 구조체를 얻습니다.
	DXGI_ADAPTER_DESC AdapterDesc;
	if (FAILED(Adapter->GetDesc(&AdapterDesc)))
		return;

	// 비디오카드 메모리 용량 단위를 MB 단위로 저장합니다.
	int VideoCardMemory = (int)(AdapterDesc.DedicatedVideoMemory / (1024 * 1024));

	// 비디오카드 이름을 저장합니다.
	char VideoCardDescription[128];
	size_t StrLen = 0;
	if (wcstombs_s(&StrLen, VideoCardDescription, sizeof(VideoCardDescription), AdapterDesc.Description, sizeof(AdapterDesc.Description)))
		return;

	// 디스플레이 모드 리스트를 해제합니다.
	delete[] DisplayModeList;
	DisplayModeList = nullptr;

	// 출력 어뎁터를 해제합니다.
	AdapterOutput->Release();
	AdapterOutput = nullptr;

	// 어댑터를 해제합니다.
	Adapter->Release();
	Adapter = nullptr;

	// 팩토리 객체를 해제합니다.
	Factory->Release();
	Factory = nullptr;

	// 스왑체인 구조체를 초기화합니다.
	DXGI_SWAP_CHAIN_DESC SwapChainDesc;
	ZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));

	SwapChainDesc.BufferCount = 1;	// 백버퍼 1개만 사용하도록 지정
	SwapChainDesc.BufferDesc.Width = SCR_WIDTH;
	SwapChainDesc.BufferDesc.Height = SCR_HEIGHT;
	SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	// 32Bit 서페이스를 설정

	if (EnableVsync)
	{
		SwapChainDesc.BufferDesc.RefreshRate.Numerator = Numerator;
		SwapChainDesc.BufferDesc.RefreshRate.Denominator = Denominator;
	}
	else
	{
		SwapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
		SwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	}
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.OutputWindow = hWnd;	// 렌더링에 사용될 윈도우 핸들 지정
	SwapChainDesc.SampleDesc.Count = 1;
	SwapChainDesc.SampleDesc.Quality = 0;
	SwapChainDesc.Windowed = !InFullScreen;

	// 스캔라인 순서 및 크기를 지정하지 않음으로 설정
	SwapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	SwapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;	// 출력된 다음 백버퍼를 비우도록 지정
	SwapChainDesc.Flags = 0;

	// 피쳐레벨을 DX11로 설정
	D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_0;

	// 스왑체인, Direct3D 장치 그리고 Direct3D 장치 컨텍스트를 만듭니다.
	if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &FeatureLevel, 1
		, D3D11_SDK_VERSION, &SwapChainDesc, &SwapChain, &Device, nullptr, &DeviceContext)))
	{
		return;
	}

	// 백버퍼 포인터를 얻어옵니다.
	ID3D11Texture2D* Backbuffer = nullptr;
	if (FAILED(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&Backbuffer)))
		return;

	// 백버퍼 포인터로 렌더타겟 뷰를 생성
	if (FAILED(Device->CreateRenderTargetView(Backbuffer, nullptr, &RenderTargetView)))
		return;

	// 백버퍼 포인터를 해제합니다.
	Backbuffer->Release();
	Backbuffer = nullptr;

	// 깊이버퍼 구조체를 초기화 합니다.
	D3D11_TEXTURE2D_DESC DepthBufferDesc;
	ZeroMemory(&DepthBufferDesc, sizeof(DepthBufferDesc));

	DepthBufferDesc.Width = SCR_WIDTH;
	DepthBufferDesc.Height = SCR_HEIGHT;
	DepthBufferDesc.MipLevels = 1;
	DepthBufferDesc.ArraySize = 1;
	DepthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DepthBufferDesc.SampleDesc.Count = 1;
	DepthBufferDesc.SampleDesc.Quality = 0;
	DepthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	DepthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	DepthBufferDesc.CPUAccessFlags = 0;
	DepthBufferDesc.MiscFlags = 0;

	// 깊이버퍼 텍스쳐 생성
	if (Device->CreateTexture2D(&DepthBufferDesc, nullptr, &DepthStencilBuffer))
		return;

	// 스텐실상태 구조체를 초기화합니다.
	D3D11_DEPTH_STENCIL_DESC DepthStencilDesc;
	ZeroMemory(&DepthStencilDesc, sizeof(DepthStencilDesc));

	DepthStencilDesc.DepthEnable = true;
	DepthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	DepthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

	DepthStencilDesc.StencilEnable = true;
	DepthStencilDesc.StencilReadMask = 0xFF;
	DepthStencilDesc.StencilWriteMask = 0xFF;

	DepthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	DepthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	DepthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	DepthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	DepthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	if (FAILED(Device->CreateDepthStencilState(&DepthStencilDesc, &DepthStencilState)))
		return;

	// Set DepthStencilState
	DeviceContext->OMSetDepthStencilState(DepthStencilState, 1);

	D3D11_DEPTH_STENCIL_VIEW_DESC DepthStencilViewDesc;
	ZeroMemory(&DepthStencilViewDesc, sizeof(DepthStencilViewDesc));
	DepthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DepthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DepthStencilViewDesc.Texture2D.MipSlice = 0;

	// 깊이 스텐실 뷰를 생성
	if (FAILED(Device->CreateDepthStencilView(DepthStencilBuffer, &DepthStencilViewDesc, &DepthStencilView)))
		return;

	// Set RenderTargetView and DepthStencilView
	DeviceContext->OMSetRenderTargets(1, &RenderTargetView, DepthStencilView);

	D3D11_RASTERIZER_DESC RasterDesc;
	RasterDesc.AntialiasedLineEnable = false;
	RasterDesc.CullMode = D3D11_CULL_BACK;
	RasterDesc.DepthBias = 0;
	RasterDesc.DepthBiasClamp = 0.0f;
	RasterDesc.DepthClipEnable = true;
	RasterDesc.FillMode = D3D11_FILL_SOLID;
	RasterDesc.FrontCounterClockwise = false;
	RasterDesc.MultisampleEnable = false;
	RasterDesc.ScissorEnable = false;
	RasterDesc.SlopeScaledDepthBias = 0.0f;

	if (FAILED(Device->CreateRasterizerState(&RasterDesc, &RasterState)))
		return;

	// Set RasterizerState
	DeviceContext->RSSetState(RasterState);

	D3D11_VIEWPORT Viewport;
	Viewport.Width = (float)SCR_WIDTH;
	Viewport.Height = (float)SCR_HEIGHT;
	Viewport.MinDepth = 0.0f;
	Viewport.MaxDepth = 1.0f;
	Viewport.TopLeftX = 0.0f;
	Viewport.TopLeftY = 0.0f;

	DeviceContext->RSSetViewports(1, &Viewport);

	float FieldOfView = 3.14f / 4.0f;
	float ScreenAspect = (float)SCR_WIDTH / (float)SCR_HEIGHT;

	ProjectionMatrix = XMMatrixPerspectiveFovLH(FieldOfView, ScreenAspect, 1.0f, 1000.0f);
	WorldMatrix = XMMatrixIdentity();
	OrthoMatrix = XMMatrixOrthographicLH((float)SCR_WIDTH, (float)SCR_HEIGHT, 1.0f, 1000.0f);

	InitializeBuffers(Device);
	InitializeShader(Device, hWnd, TEXT("Shaders/HLSL/Color.vs"), TEXT("Shaders/HLSL/Color.ps"));

	ShowWindow(hWnd, SW_SHOW);

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

}
