#include "pch.h"
#include "jRHI_DX12.h"

void jCreatedResource::Free()
{
    if (Resource)
    {
        if (ResourceType == jCreatedResource::EType::Standalone)
        {
            if (g_rhi_dx12)
                g_rhi_dx12->DeallocatorMultiFrameStandaloneResource.Free(Resource);
        }
        else if (ResourceType == jCreatedResource::EType::ResourcePool)
        {
            if (g_rhi_dx12)
                g_rhi_dx12->DeallocatorMultiFramePlacedResource.Free(Resource);
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
