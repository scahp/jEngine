#include "pch.h"
#include "jPipelineStateInfo.h"
#include "Vulkan/jVertexBuffer_Vulkan.h"

size_t jPipelineStateInfo::GetHash() const
{
    if (Hash)
        return Hash;

    Hash = 0;
    if (IsGraphics)
    {
        check(PipelineStateFixed);
        Hash ^= PipelineStateFixed->CreateHash();
        Hash ^= VertexBufferArray.GetHash();
        Hash ^= RenderPass->GetHash();
        Hash ^= GraphicsShader.GetHash();
    }
    else
    {
        check(ComputeShader);
        Hash ^= ComputeShader->ShaderInfo.GetHash();
    }

    Hash ^= ShaderBindingLayoutArray.GetHash();

    if (PushConstant)
    {
        Hash ^= PushConstant->GetHash();
    }
    Hash ^= SubpassIndex;

    return Hash;
}

