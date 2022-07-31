#include "pch.h"
#include "jPipelineStateInfo.h"
#include "Vulkan/jVertexBuffer_Vulkan.h"

size_t jPipelineStateInfo::GetHash() const
{
    if (Hash)
        return Hash;

    check(PipelineStateFixed);
    Hash = PipelineStateFixed->CreateHash();

    Hash ^= Shader->ShaderInfo.GetHash();
    Hash ^= VertexBuffer->GetHash();
    Hash ^= jShaderBindings::CreateShaderBindingsHash(ShaderBindings);
    Hash ^= RenderPass->GetHash();

    return Hash;
}

