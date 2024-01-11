#include "pch.h"
#include "jPipelineStateInfo.h"
#include "Vulkan/jVertexBuffer_Vulkan.h"

size_t jPipelineStateInfo::GetHash() const
{
    if (Hash)
        return Hash;

    Hash = 0;
    if (PipelineType == EPipelineType::Graphics)
    {
        check(PipelineStateFixed);
        Hash ^= PipelineStateFixed->CreateHash();
        Hash ^= VertexBufferArray.GetHash();
        Hash ^= RenderPass->GetHash();
        Hash ^= GraphicsShader.GetHash();
    }
    else if (PipelineType == EPipelineType::Compute)
    {
        check(ComputeShader);
        Hash ^= ComputeShader->ShaderInfo.GetHash();
    }
    else if (PipelineType == EPipelineType::RayTracing)
    {
        for (int32 i = 0; i < (int32)RaytracingShaders.size(); ++i)
        {
            Hash ^= RaytracingShaders[i].GetHash();
        }
        Hash ^= RaytracingPipelineData.GetHash();
    }
    else
    {
        check(0);
    }

    Hash ^= ShaderBindingLayoutArray.GetHash();

    if (PushConstant)
    {
        Hash ^= PushConstant->GetHash();
    }
    Hash ^= SubpassIndex;

    return Hash;
}

