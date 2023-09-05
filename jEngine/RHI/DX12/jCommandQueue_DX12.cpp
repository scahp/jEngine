#include "pch.h"
#include "jCommandQueue_DX12.h"

void jCommandBuffer_DX12::Reset()
{
    CommandAllocator->Reset();
    CommandList->Reset(CommandAllocator.Get(), nullptr);
    OnlineDescriptorHeap->Reset();
    OnlineSamplerDescriptorHeap->Reset();
}

bool jCommandBuffer_DX12::Close()
{
    if (FAILED(CommandList->Close()))
        return false;

	return true;
}

bool jCommandQueue_DX12::Initialize(ComPtr<ID3D12Device> InDevice, D3D12_COMMAND_LIST_TYPE InType)
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

uint64 jCommandQueue_DX12::ExecuteCommandList(jCommandBuffer_DX12* InCommandList)
{
	if (!InCommandList->Close())
		return -1;

	//ID3D12CommandAllocator* commandAllocator = nullptr;
	//uint32 dataSize = sizeof(commandAllocator);
	//if (FAILED(InCommandList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator)))
	//	return -1;

	ID3D12CommandList* pCommandLists[] = { InCommandList->Get() };
	CommandQueue->ExecuteCommandLists(1, pCommandLists);
	const uint64 fenceValue = Signal();

	//AvailableCommandAllocatorQueue.emplace(jCommandAllocatorEntry(fenceValue, commandAllocator));
	InCommandList->FenceValue = fenceValue;
	AvailableCommandLists.push_back(InCommandList);

    //InCommandList->CommandAllocator->Release();

	//commandAllocator->Release();

	return fenceValue;
}

jCommandBuffer_DX12* jCommandQueue_DX12::CreateCommandList() const
{
    jCommandBuffer_DX12* commandList = new jCommandBuffer_DX12();
	commandList->CommandAllocator = CreateCommandAllocator();
	if (FAILED(Device->CreateCommandList(0, CommandListType
		, commandList->CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList->CommandList))))
	{
		delete commandList;
		return nullptr;
	}

    commandList->OnlineDescriptorHeap = g_rhi_dx12->OnlineDescriptorHeapBlocks.Alloc();
	commandList->OnlineSamplerDescriptorHeap = g_rhi_dx12->OnlineSamplerDescriptorHeapBlocks.Alloc();

    return commandList;
}

//ComPtr<ID3D12CommandAllocator> jCommandQueue_DX12::GetAvailableAllocator()
//{
//	ComPtr<ID3D12CommandAllocator> CommandAllocator;
//	if (!AvailableCommandAllocatorQueue.empty() && IsFenceComplete(AvailableCommandAllocatorQueue.front().FenceValue))
//	{
//		CommandAllocator = AvailableCommandAllocatorQueue.front().CommandAllocator;
//		AvailableCommandAllocatorQueue.pop();
//
//		if (FAILED(CommandAllocator->Reset()))
//			return nullptr;
//	}
//	else
//	{
//		CommandAllocator = CreateCommandAllocator();
//	}
//	return CommandAllocator;
//}

jCommandBuffer_DX12* jCommandQueue_DX12::GetAvailableCommandList()
{
	for(int32 i=0;i<AvailableCommandLists.size();++i)
	{
		if (IsFenceComplete(AvailableCommandLists[i]->FenceValue))
		{
			jCommandBuffer_DX12* SelectedCmdBuffer = AvailableCommandLists[i];
			AvailableCommandLists.erase(AvailableCommandLists.begin() + i);
			SelectedCmdBuffer->Reset();
			return SelectedCmdBuffer;
		}
	}

	return CreateCommandList();
}

