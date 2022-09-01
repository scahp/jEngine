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

        for (int32 i = 0; i < VertexBuffers.size(); ++i)
        {
            if (VertexBuffers[i])
                Hash = CityHash64WithSeed(VertexBuffers[i]->GetHash(), Hash);
        }

        Hash = CityHash64WithSeed(RenderPass->GetHash(), Hash);
    }

    Hash = CityHash64WithSeed(Shader->ShaderInfo.GetHash(), Hash);
    Hash = CityHash64WithSeed(jShaderBindingsLayout::CreateShaderBindingLayoutHash(ShaderBindings), Hash);

    if (PushConstant)
    {
        Hash = CityHash64WithSeed(PushConstant->GetHash(), Hash);
    }    

    return Hash;
}

