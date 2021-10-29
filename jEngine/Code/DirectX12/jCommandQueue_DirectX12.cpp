#include "pch.h"
#include "jCommandQueue_DirectX12.h"

bool jCommandQueue_DirectX12::Initialize(ComPtr<ID3D12Device> InDevice, D3D12_COMMAND_LIST_TYPE InType)
{
	JASSERT(InDevice);
	Device = InDevice;

	CommandListType = InType;

	if (CommandListType != D3D12_COMMAND_LIST_TYPE_BUNDLE)
	{
		D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
		commandQueueDesc.Type = CommandListType;
		commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		commandQueueDesc.NodeMask = 0;

		if (FAILED(Device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&CommandQueue))))
			return false;
	}

	if (FAILED(Device->CreateFence(FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence))))
		return false;

	FenceEvent = ::CreateEvent(nullptr, false, false, nullptr);
	if (!FenceEvent)
		return false;

	return true;
}

uint64 jCommandQueue_DirectX12::ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList2> InCommandListPtr)
{
	if (FAILED(InCommandListPtr->Close()))
		return -1;

	ID3D12CommandAllocator* commandAllocator = nullptr;
	uint32 dataSize = sizeof(commandAllocator);
	if (FAILED(InCommandListPtr->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator)))
		return -1;

	ID3D12CommandList* pCommandLists[] = { InCommandListPtr.Get() };
	CommandQueue->ExecuteCommandLists(1, pCommandLists);
	const uint64 fenceValue = Signal();

	AvailableCommandAllocatorQueue.emplace(jCommandAllocatorEntry(fenceValue, commandAllocator));
	AvailableCommandListQueue.push(InCommandListPtr);

	commandAllocator->Release();

	return fenceValue;
}

ComPtr<ID3D12CommandAllocator> jCommandQueue_DirectX12::GetAvailableAllocator()
{
	ComPtr<ID3D12CommandAllocator> CommandAllocator;
	if (!AvailableCommandAllocatorQueue.empty() && IsFenceComplete(AvailableCommandAllocatorQueue.front().FenceValue))
	{
		CommandAllocator = AvailableCommandAllocatorQueue.front().CommandAllocator;
		AvailableCommandAllocatorQueue.pop();

		if (FAILED(CommandAllocator->Reset()))
			return nullptr;
	}
	else
	{
		CommandAllocator = CreateCommandAllocator();
	}
	return CommandAllocator;
}

ComPtr<ID3D12GraphicsCommandList2> jCommandQueue_DirectX12::GetAvailableCommandList()
{
	ComPtr<ID3D12GraphicsCommandList2> CommandList;
	ComPtr<ID3D12CommandAllocator> AvailableCommandAllocator = GetAvailableAllocator();
	if (AvailableCommandListQueue.empty())
	{
		CommandList = CreateCommandList(AvailableCommandAllocator);
	}
	else
	{
		CommandList = AvailableCommandListQueue.front();
		AvailableCommandListQueue.pop();

		if (FAILED(CommandList->Reset(AvailableCommandAllocator.Get(), nullptr)))
			return nullptr;
	}

	if (FAILED(CommandList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), AvailableCommandAllocator.Get())))
		return nullptr;

	return CommandList;
}
