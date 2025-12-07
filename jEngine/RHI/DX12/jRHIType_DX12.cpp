#include "pch.h"
#include "jRHI_DX12.h"

std::shared_ptr<jCreatedResource> jCreatedResource::CreatedFromResourcePool(const jPlacedResource& InResource)
{
    return std::shared_ptr<jCreatedResource>(new jCreatedResource(EType::ResourcePool, InResource.PlacedSubResource, InResource.LastLayout));
}

void jCreatedResource::Free()
{
    if (Resource)
    {
        if (ResourceType == jCreatedResource::EType::Standalone)
        {
            if (g_rhi_dx12)
                g_rhi_dx12->DeallocatorMultiFrameStandaloneResource.Free(
                    jPendingDeallocateResource(Resource, Layout)
                );
        }
        else if (ResourceType == jCreatedResource::EType::ResourcePool)
        {
            if (g_rhi_dx12)
                g_rhi_dx12->DeallocatorMultiFramePlacedResource.Free(
                    jPendingDeallocateResource(Resource, Layout)
                );
        }
        else if (ResourceType == jCreatedResource::EType::Swapchain)
        {
            // Nothing todo
        }
        else
        {
            check(0);
        }
    }
}

bool IsDX12CompareEnabled(D3D12_COMPARISON_FUNC InFilter)
{
    switch (InFilter)
    {
    case D3D12_COMPARISON_FUNC_LESS:
    case D3D12_COMPARISON_FUNC_EQUAL:
    case D3D12_COMPARISON_FUNC_LESS_EQUAL:
    case D3D12_COMPARISON_FUNC_GREATER:
    case D3D12_COMPARISON_FUNC_NOT_EQUAL:
    case D3D12_COMPARISON_FUNC_GREATER_EQUAL:
        return true;

    default:
        return false; // NEVER, ALWAYS
    }
}
