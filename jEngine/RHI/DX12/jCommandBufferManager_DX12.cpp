#include "pch.h"
#include "jCommandBufferManager_DX12.h"
#include "jQueryPoolTime_DX12.h"
#include "jFenceManager_DX12.h"

jSyncAcrossCommandQueue_DX12::jSyncAcrossCommandQueue_DX12(ECommandBufferType InType, jFence_DX12* InFence, uint64 InFenceValue)
    : Type(InType), Fence(InFence), FenceValue(InFenceValue)
{
    check(InFence);
    if (InFenceValue == -1)
    {
        FenceValue = InFence->FenceValue;
    }
}

void jSyncAcrossCommandQueue_DX12::WaitSyncAcrossCommandQueue(ECommandBufferType InWaitCommandQueueType)
{
    if (!ensure(InWaitCommandQueueType != Type))
        return;

    auto CommandBufferManager = g_rhi->GetCommandBufferManager(InWaitCommandQueueType);
    check(CommandBufferManager);

    CommandBufferManager->WaitCommandQueueAcrossSync(shared_from_this());
}

jCommandBuffer_DX12::~jCommandBuffer_DX12()
{
    check(IsClosed);
}

bool jCommandBuffer_DX12::Begin() const
{
    Reset();

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
    return Owner->Fence ? Owner->Fence->GetHandle() : nullptr;
}

void jCommandBuffer_DX12::SetFence(void* InFence)
{
    // Fence = (jFence_DX12*)InFence;
    check(0);
}

jFence* jCommandBuffer_DX12::GetFence() const
{
    return Owner->Fence;
}

bool jCommandBuffer_DX12::IsCompleteForWaitFence()
{
    return Owner->Fence->IsComplete(FenceValue);
}

bool jCommandBuffer_DX12::End() const
{
    if (IsClosed)
        return true;

#if USE_RESOURCE_BARRIER_BATCHER
    FlushBarrierBatch();
#endif // USE_RESOURCE_BARRIER_BATCHER

    IsClosed = true;
    if (JFAIL(CommandList->Close()))
        return false;

    return true;
}

jCommandBufferManager_DX12::jCommandBufferManager_DX12(ECommandBufferType InType)
    : CommandBufferType(InType), CommandListType(GetDX12CommandBufferType(InType))
{

}

void jCommandBufferManager_DX12::Release()
{
    jScopedLock s(&CommandListLock);

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

    CommandQueue.Reset();
}

jCommandBuffer_DX12* jCommandBufferManager_DX12::GetOrCreateCommandBuffer()
{
    jScopedLock s(&CommandListLock);

    jCommandBuffer_DX12* SelectedCmdBuffer = nullptr;
    for (int32 i = 0; i < AvailableCommandLists.size(); ++i)
    {
        if (AvailableCommandLists[i]->IsCompleteForWaitFence())
        {
            SelectedCmdBuffer = AvailableCommandLists[i];
            AvailableCommandLists.erase(AvailableCommandLists.begin() + i);
            break;
        }
    }

    if (!SelectedCmdBuffer)
    {
        SelectedCmdBuffer = CreateCommandList();
        check(SelectedCmdBuffer);
    }

    UsingCommandBuffers.push_back(SelectedCmdBuffer);
    SelectedCmdBuffer->Begin();
    return SelectedCmdBuffer;
}

void jCommandBufferManager_DX12::ReturnCommandBuffer(jCommandBuffer* commandBuffer)
{
    jScopedLock s(&CommandListLock);

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

bool jCommandBufferManager_DX12::Initialize(ComPtr<ID3D12Device> InDevice)
{
    JASSERT(InDevice);
    Device = InDevice;

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

    Fence = (jFence_DX12*)g_rhi_dx12->FenceManager.GetOrCreateFence();

    return true;
}

std::shared_ptr<jSyncAcrossCommandQueue_DX12> jCommandBufferManager_DX12::ExecuteCommandList(jCommandBuffer_DX12* InCommandList, bool bWaitUntilExecuteComplete)
{
    std::shared_ptr< jSyncAcrossCommandQueue_DX12> CommandQueueSyncObjectPtr;
    if (InCommandList->End())
    {
        ID3D12CommandList* pCommandLists[] = { InCommandList->Get() };
        CommandQueue->ExecuteCommandLists(1, pCommandLists);

        if (ensure(InCommandList->Owner->Fence))
        {
            InCommandList->FenceValue = InCommandList->Owner->Fence->SignalWithNextFenceValue(InCommandList->Owner->CommandQueue.Get(), bWaitUntilExecuteComplete);
            CommandQueueSyncObjectPtr = std::make_shared<jSyncAcrossCommandQueue_DX12>(InCommandList->Type, InCommandList->Owner->Fence);
        }
    }
    return CommandQueueSyncObjectPtr;
}

void jCommandBufferManager_DX12::WaitCommandQueueAcrossSync(const std::shared_ptr<jSyncAcrossCommandQueue>& InSync)
{
    check(CommandQueue);
    check(InSync);

    auto Sync_DX12 = (jSyncAcrossCommandQueue_DX12*)InSync.get();
    check(Sync_DX12->Fence);
    CommandQueue->Wait(Sync_DX12->Fence->Fence.Get(), Sync_DX12->FenceValue);
}

jQueryPool* jCommandBufferManager_DX12::GetQueryTimePool() const
{
    return QueryPoolTime;
}

jCommandBuffer_DX12* jCommandBufferManager_DX12::CreateCommandList() const
{
    jCommandBuffer_DX12* commandBuffer = new jCommandBuffer_DX12(GetDX12CommandBufferType(CommandListType));
    commandBuffer->Owner = this;
    commandBuffer->CommandAllocator = CreateCommandAllocator();
    if (FAILED(Device->CreateCommandList(0, CommandListType
        , commandBuffer->CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandBuffer->CommandList))))
    {
        delete commandBuffer;
        return nullptr;
    }

    if (D3D12_COMMAND_LIST_TYPE_COPY != CommandListType)
    {
        commandBuffer->OnlineDescriptorHeap = g_rhi_dx12->OnlineDescriptorHeapManager.Alloc(EDescriptorHeapTypeDX12::CBV_SRV_UAV);
        commandBuffer->OnlineSamplerDescriptorHeap = g_rhi_dx12->OnlineDescriptorHeapManager.Alloc(EDescriptorHeapTypeDX12::SAMPLER);

        check(commandBuffer->OnlineDescriptorHeap);
        check(commandBuffer->OnlineSamplerDescriptorHeap);
    }

    return commandBuffer;
}

