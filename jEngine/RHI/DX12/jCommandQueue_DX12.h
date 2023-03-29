#pragma once

#include <queue>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class jCommandQueue_DX12
{
public:
	jCommandQueue_DX12()
		: FenceValue(0)
		, CommandListType(D3D12_COMMAND_LIST_TYPE_DIRECT)
	{ }

	virtual ~jCommandQueue_DX12() {}

	bool Initialize(ComPtr<ID3D12Device> InDevice, D3D12_COMMAND_LIST_TYPE InType = D3D12_COMMAND_LIST_TYPE_DIRECT);

	// Fence
	FORCEINLINE uint64 Signal()
	{
		CommandQueue->Signal(Fence.Get(), ++FenceValue);
		return FenceValue;
	}
	FORCEINLINE bool IsFenceComplete(uint64 InFenceValue) const
	{
		return Fence->GetCompletedValue() >= InFenceValue;
	}
	FORCEINLINE void WaitForFenceValue() const { WaitForFenceValue(FenceValue); }
	FORCEINLINE void WaitForFenceValue(uint64 InFenceValue) const
	{
		if (!IsFenceComplete(InFenceValue))
		{
			Fence->SetEventOnCompletion(InFenceValue, FenceEvent);
			::WaitForSingleObject(FenceEvent, INFINITE);
		}
	}
	FORCEINLINE void Flush()
	{
		WaitForFenceValue(Signal());
	}
	//////////////////////////////////////////////////////////////////////////

	FORCEINLINE ComPtr<ID3D12CommandQueue> GetCommandQueue() const { return CommandQueue; }

	// CommandList
	uint64 ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList4> InCommandListPtr);
	ComPtr<ID3D12GraphicsCommandList4> GetAvailableCommandList();

private:
	FORCEINLINE ComPtr<ID3D12CommandAllocator> CreateCommandAllocator() const
	{
		ComPtr<ID3D12CommandAllocator> commandAllocator;
		if (FAILED(Device->CreateCommandAllocator(CommandListType, IID_PPV_ARGS(&commandAllocator))))
			return nullptr;

		return commandAllocator;
	}

	FORCEINLINE ComPtr<ID3D12GraphicsCommandList4> CreateCommandList(ComPtr<ID3D12CommandAllocator> InAllocatorPtr) const
	{
		ComPtr<ID3D12GraphicsCommandList4> commandList;
		if (FAILED(Device->CreateCommandList(0, CommandListType, InAllocatorPtr.Get(), nullptr, IID_PPV_ARGS(&commandList))))
			return nullptr;

		return commandList;
	}

	ComPtr<ID3D12CommandAllocator> GetAvailableAllocator();

	struct jCommandAllocatorEntry
	{
		jCommandAllocatorEntry() = default;
		jCommandAllocatorEntry(const uint64 InFenceValue, const ComPtr<ID3D12CommandAllocator>& InCommandAllocator)
			: FenceValue(InFenceValue), CommandAllocator(InCommandAllocator)
		{ }

		uint64 FenceValue;
		ComPtr<ID3D12CommandAllocator> CommandAllocator;
	};

	std::queue<jCommandAllocatorEntry> AvailableCommandAllocatorQueue;
	std::queue<ComPtr<ID3D12GraphicsCommandList4>> AvailableCommandListQueue;

	D3D12_COMMAND_LIST_TYPE CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ComPtr<ID3D12Device> Device;
	ComPtr<ID3D12CommandQueue> CommandQueue;
	ComPtr<ID3D12Fence> Fence;
	HANDLE FenceEvent = nullptr;
	uint64 FenceValue = 0;
};