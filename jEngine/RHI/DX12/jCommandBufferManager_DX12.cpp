#include "pch.h"
#include "jCommandBufferManager_DX12.h"

bool jCommandBuffer_DX12::Begin() const
{
    ensure(OnlineDescriptorHeap && OnlineSamplerDescriptorHeap || (!OnlineDescriptorHeap && !OnlineSamplerDescriptorHeap));
    if (OnlineDescriptorHeap && OnlineSamplerDescriptorHeap)
    {
        ID3D12DescriptorHeap* ppHeaps[] =
        {
            OnlineDescriptorHeap->GetHeap(),
            OnlineSamplerDescriptorHeap->GetHeap()
        };
        CommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    }

    return true;
}

void jCommandBuffer_DX12::Reset() const
{
    if (IsClosed)
    {
        CommandAllocator->Reset();
        CommandList->Reset(CommandAllocator.Get(), nullptr);
        if (OnlineDescriptorHeap)
            OnlineDescriptorHeap->Reset();
        if (OnlineSamplerDescriptorHeap)
            OnlineSamplerDescriptorHeap->Reset();
        IsClosed = false;
    }
}

void* jCommandBuffer_DX12::GetFenceHandle() const
{
    return Fence ? Fence->GetHandle() : nullptr;
}

void jCommandBuffer_DX12::SetFence(void* InFence)
{
    Fence = (jFence_DX12*)InFence;
}

jFence* jCommandBuffer_DX12::GetFence() const
{
    return Fence;
}

bool jCommandBuffer_DX12::End() const
{
    if (IsClosed)
        return true;

    if (FAILED(CommandList->Close()))
        return false;

    IsClosed = true;
    return true;
}

void jCommandBufferManager_DX12::Release()
{
    for (auto& iter : UsingCommandBuffers)
    {
        iter->GetFence()->WaitForFence();
        g_rhi->GetFenceManager()->ReturnFence(iter->GetFence());
        delete iter;
    }
    UsingCommandBuffers.clear();

    for (auto& iter : AvailableCommandLists)
    {
        iter->GetFence()->WaitForFence();
        g_rhi->GetFenceManager()->ReturnFence(iter->GetFence());
        delete iter;
    }
    AvailableCommandLists.clear();
}

jCommandBuffer_DX12* jCommandBufferManager_DX12::GetOrCreateCommandBuffer()
{
    jCommandBuffer_DX12* SelectedCmdBuffer = nullptr;
    for (int32 i = 0; i < AvailableCommandLists.size(); ++i)
    {
        if (AvailableCommandLists[i]->Fence->IsComplete())
        {
            SelectedCmdBuffer = AvailableCommandLists[i];
            AvailableCommandLists.erase(AvailableCommandLists.begin() + i);
            SelectedCmdBuffer->Reset();
            break;
        }
    }

    if (!SelectedCmdBuffer)
    {
        SelectedCmdBuffer = CreateCommandList();
        check(SelectedCmdBuffer);
    }

    UsingCommandBuffers.push_back(SelectedCmdBuffer);

    return SelectedCmdBuffer;
}

void jCommandBufferManager_DX12::ReturnCommandBuffer(jCommandBuffer* commandBuffer)
{
    for (int32 i = 0; i < UsingCommandBuffers.size(); ++i)
    {
        if (UsingCommandBuffers[i]->GetHandle() == commandBuffer->GetHandle())
        {
            UsingCommandBuffers.erase(UsingCommandBuffers.begin() + i);
            AvailableCommandLists.push_back((jCommandBuffer_DX12*)commandBuffer);
            return;
        }
    }
}

bool jCommandBufferManager_DX12::Initialize(ComPtr<ID3D12Device> InDevice, D3D12_COMMAND_LIST_TYPE InType)
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

    return true;
}

void jCommandBufferManager_DX12::ExecuteCommandList(jCommandBuffer_DX12* InCommandList, bool bWaitUntilExecuteComplete)
{
    if (!InCommandList->End())
        return;

    ID3D12CommandList* pCommandLists[] = { InCommandList->Get() };
    CommandQueue->ExecuteCommandLists(1, pCommandLists);
    if (ensure(InCommandList->Fence))
        InCommandList->Fence->SignalWithNextFenceValue(CommandQueue.Get(), bWaitUntilExecuteComplete);
}

jCommandBuffer_DX12* jCommandBufferManager_DX12::CreateCommandList() const
{
    jCommandBuffer_DX12* commandBuffer = new jCommandBuffer_DX12();
    commandBuffer->CommandAllocator = CreateCommandAllocator();
    if (FAILED(Device->CreateCommandList(0, CommandListType
        , commandBuffer->CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandBuffer->CommandList))))
    {
        delete commandBuffer;
        return nullptr;
    }

    commandBuffer->SetFence(g_rhi_dx12->FenceManager.GetOrCreateFence());
    if (D3D12_COMMAND_LIST_TYPE_COPY != CommandListType)
    {
        commandBuffer->OnlineDescriptorHeap = g_rhi_dx12->OnlineDescriptorHeapManager.Alloc(EDescriptorHeapTypeDX12::CBV_SRV_UAV);
        commandBuffer->OnlineSamplerDescriptorHeap = g_rhi_dx12->OnlineDescriptorHeapManager.Alloc(EDescriptorHeapTypeDX12::SAMPLER);

        check(commandBuffer->OnlineDescriptorHeap);
        check(commandBuffer->OnlineSamplerDescriptorHeap);
    }

    return commandBuffer;
}

