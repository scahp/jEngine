#pragma once

#include "../jCommandBufferManager.h"
#include "jResourceBarrierBatcher_DX12.h"

class jFence_DX12;
struct jQueryPoolTime_DX12;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// DX12 is using Fence for sync between CommandQueues
struct jCommandQueueAcrossSyncObject_DX12 : public jCommandQueueAcrossSyncObject
{
    jCommandQueueAcrossSyncObject_DX12(ECommandBufferType InType, jFence_DX12* InFence, uint64 InFenceValue = -1);
    virtual ~jCommandQueueAcrossSyncObject_DX12() {}
  
    virtual void WaitCommandQueueAcrossSync(ECommandBufferType InWaitCommandQueueType) override;

    ECommandBufferType Type = ECommandBufferType::MAX;
    jFence_DX12* Fence = nullptr;
    uint64 FenceValue = 0;
};

struct jCommandBuffer_DX12 : public jCommandBuffer
{
    jCommandBuffer_DX12(ECommandBufferType InType) : jCommandBuffer(InType) {}
    virtual ~jCommandBuffer_DX12() {}

    ID3D12GraphicsCommandList4* Get() const
    {
        return CommandList.Get();
    }
    bool IsValid() const
    {
        return CommandList.Get();
    }
    
    FORCEINLINE ComPtr<ID3D12GraphicsCommandList4>& GetRef() { return CommandList; }
    virtual bool Begin() const override;
    virtual bool End() const override;
    virtual void Reset() const override;

    virtual void* GetHandle() const override { return CommandList.Get(); }
    virtual void* GetFenceHandle() const override;
    virtual void SetFence(void* InFence) override;
    virtual jFence* GetFence() const override;
    virtual bool IsEnd() const override { return IsClosed; }
    virtual jResourceBarrierBatcher* GetBarrierBatcher() const override { return &BarrierBatcher; }
    virtual void FlushBarrierBatch() const override { BarrierBatcher.Flush(this); }

    const class jCommandBufferManager_DX12* Owner = nullptr;
    bool IsCompleteForWaitFence();

//private:
    uint64 FenceValue = 0;
    ComPtr<ID3D12CommandAllocator> CommandAllocator;
    ComPtr<ID3D12GraphicsCommandList4> CommandList;

    class jOnlineDescriptorHeap_DX12* OnlineDescriptorHeap = nullptr;
    class jOnlineDescriptorHeap_DX12* OnlineSamplerDescriptorHeap = nullptr;

    mutable bool IsClosed = false;
    mutable jResourceBarrierBatcher_DX12 BarrierBatcher;
};

class jCommandBufferManager_DX12 : public jCommandBufferManager
{
public:
    jCommandBufferManager_DX12(ECommandBufferType InType);

    virtual ~jCommandBufferManager_DX12() {}

    virtual void Release() override;

    virtual jCommandBuffer_DX12* GetOrCreateCommandBuffer() override;
    virtual void ReturnCommandBuffer(jCommandBuffer* commandBuffer) override;

    bool Initialize(ComPtr<ID3D12Device> InDevice);

    FORCEINLINE ComPtr<ID3D12CommandQueue> GetCommandQueue() const { return CommandQueue; }

    // CommandList
    std::shared_ptr<jCommandQueueAcrossSyncObject_DX12> ExecuteCommandList(jCommandBuffer_DX12* InCommandList, bool bWaitUntilExecuteComplete = false);
    virtual void WaitCommandQueueAcrossSync(const std::shared_ptr<jCommandQueueAcrossSyncObject>& InSync) override;

    virtual jQueryPool* GetQueryTimePool() const override;

    jFence_DX12* Fence = nullptr;                        // Fence manager 에서 참조하기 때문에 소멸시키지 않음

private:
    FORCEINLINE ComPtr<ID3D12CommandAllocator> CreateCommandAllocator() const
    {
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        if (FAILED(Device->CreateCommandAllocator(CommandListType, IID_PPV_ARGS(&commandAllocator))))
            return nullptr;

        return commandAllocator;
    }

    jCommandBuffer_DX12* CreateCommandList() const;

    jMutexLock CommandListLock;
    std::vector<jCommandBuffer_DX12*> AvailableCommandLists;
    mutable std::vector<jCommandBuffer_DX12*> UsingCommandBuffers;

    ECommandBufferType CommandBufferType = ECommandBufferType::GRAPHICS;
    D3D12_COMMAND_LIST_TYPE CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> CommandQueue;
    jQueryPoolTime_DX12* QueryPoolTime = nullptr;
};
