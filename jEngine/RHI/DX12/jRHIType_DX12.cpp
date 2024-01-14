#include "pch.h"
#include "jRHI_DX12.h"

void jCreatedResource::Free()
{
    if (Resource)
    {
        if (ResourceType == jCreatedResource::EType::Standalone)
        {
            Resource.Reset();
        }
        else if (ResourceType == jCreatedResource::EType::ResourcePool)
        {
            if (g_rhi_dx12)
                g_rhi_dx12->DeallocatorMultiFrameCreatedResource.Free(Resource);
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
