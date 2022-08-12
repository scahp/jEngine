#include "pch.h"
#include "jPipelineStateInfo.h"
#include "Vulkan/jVertexBuffer_Vulkan.h"

size_t jPipelineStateInfo::GetHash() const
{
    if (Hash)
        return Hash;

    if (IsGraphics)
    {
        check(PipelineStateFixed);
        Hash = PipelineStateFixed->CreateHash();

        Hash ^= VertexBuffer->GetHash();
        Hash ^= RenderPass->GetHash();
    }

    Hash ^= Shader->ShaderInfo.GetHash();
    Hash ^= jShaderBindings::CreateShaderBindingsHash(ShaderBindings);

    return Hash;
}

