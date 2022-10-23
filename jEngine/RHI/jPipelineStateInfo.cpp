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
    }

    Hash ^= Shader->ShaderInfo.GetHash();

    Hash ^= ShaderBindingLayoutArray.GetHash();

    if (PushConstant)
    {
        Hash ^= PushConstant->GetHash();
    }    

    return Hash;
}

