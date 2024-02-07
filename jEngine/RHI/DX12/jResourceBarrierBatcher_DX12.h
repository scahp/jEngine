#pragma once
#include "RHI/jResourceBarrierBatcher.h"

class jResourceBarrierBatcher_DX12 : public jResourceBarrierBatcher
{
public:
    virtual ~jResourceBarrierBatcher_DX12() {}

    virtual void AddUAV(jBuffer* InBuffer) override;
    virtual void AddUAV(jTexture* InTexture) override;
    FORCEINLINE void AddUAV_Internal(ID3D12Resource* InResource)
    {
        check(InResource);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = InResource;

        Barriers.emplace_back(barrier);
    }

    virtual void AddTransition(jBuffer* InBuffer, EResourceLayout InNewLayout) override;
    virtual void AddTransition(jTexture* InTexture, EResourceLayout InNewLayout) override;
    FORCEINLINE void AddTransition_Internal(ID3D12Resource* InResource, D3D12_RESOURCE_STATES InBefore
        , D3D12_RESOURCE_STATES InAfter, uint32 InSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)           // todo : each subresource control
    {
        check(InResource);

        D3D12_RESOURCE_BARRIER barrier = { };
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = InResource;
        barrier.Transition.StateBefore = InBefore;
        barrier.Transition.StateAfter = InAfter;
        barrier.Transition.Subresource = InSubresource;

        Barriers.emplace_back(barrier);
    }

    virtual void Flush(const jCommandBuffer* InCommandBuffer) override;

private:
    std::vector<D3D12_RESOURCE_BARRIER> Barriers;
};
