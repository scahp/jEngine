#pragma once

#include "../jCommandBufferManager.h"

class jFence_DX12;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct jCommandBuffer_DX12 : public jCommandBuffer
{
    ComPtr<ID3D12CommandAllocator> CommandAllocator;
    ComPtr<ID3D12GraphicsCommandList4> CommandList;
    mutable bool IsClosed = false;

    class jOnlineDescriptorHeap_DX12* OnlineDescriptorHeap = nullptr;
    class jOnlineDescriptorHeap_DX12* OnlineSamplerDescriptorHeap = nullptr;

    ID3D12GraphicsCommandList4* Get()
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

    const class jCommandBufferManager_DX12* Owner = nullptr;
    uint64 FenceValue = 0;
    bool IsCompleteForWaitFence();
};

class jCommandBufferManager_DX12 : public jCommandBufferManager
{
public:
    jCommandBufferManager_DX12()
        : CommandListType(D3D12_COMMAND_LIST_TYPE_DIRECT)
        //, FenceValue(0)
    { }

    virtual ~jCommandBufferManager_DX12() {}

    virtual void Release() override;

    virtual jCommandBuffer_DX12* GetOrCreateCommandBuffer() override;
    virtual void ReturnCommandBuffer(jCommandBuffer* commandBuffer) override;

    bool Initialize(ComPtr<ID3D12Device> InDevice, D3D12_COMMAND_LIST_TYPE InType = D3D12_COMMAND_LIST_TYPE_DIRECT);

    FORCEINLINE ComPtr<ID3D12CommandQueue> GetCommandQueue() const { return CommandQueue; }

    // CommandList
    void ExecuteCommandList(jCommandBuffer_DX12* InCommandList, bool bWaitUntilExecuteComplete = false);

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

    std::vector<jCommandBuffer_DX12*> AvailableCommandLists;
    mutable std::vector<jCommandBuffer_DX12*> UsingCommandBuffers;

    D3D12_COMMAND_LIST_TYPE CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> CommandQueue;
};
