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

        Hash = CityHash64WithSeed(VertexBuffer->GetHash(), Hash);
        Hash = CityHash64WithSeed(RenderPass->GetHash(), Hash);
    }

    Hash = CityHash64WithSeed(Shader->ShaderInfo.GetHash(), Hash);
    Hash = CityHash64WithSeed(jShaderBindingLayout::CreateShaderBindingLayoutHash(ShaderBindings), Hash);

    return Hash;
}

